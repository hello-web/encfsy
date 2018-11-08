
#include "EncFSFile.h"

using namespace std;

static AutoSeededX917RNG<CryptoPP::AES> random;

namespace EncFS {
	bool EncFSFile::getFileIV(const LPCWSTR FileName, int64_t *fileIv, bool create) {
		if (this->fileIvAvailable) {
			*fileIv = this->fileIv;
			return true;
		}
		// ファイルヘッダ読み込み
		LARGE_INTEGER distanceToMove;
		distanceToMove.QuadPart = 0;
		if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
			return false;
		}
		string fileHeader;
		fileHeader.resize(EncFSVolume::HEADER_SIZE);

		DWORD ReadLength;
		if (!ReadFile(this->handle, &fileHeader[0], (DWORD)fileHeader.size(), (LPDWORD)&ReadLength, NULL)) {
			return false;
		}
		if (ReadLength != fileHeader.size()) {
			if (!create) {
				return false;
			}
			// ファイルヘッダ新規作成
			fileHeader.resize(EncFSVolume::HEADER_SIZE);
			random.GenerateBlock((byte*)&fileHeader[0], EncFSVolume::HEADER_SIZE);
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return false;
			}
			DWORD writtenLen;
			if (!WriteFile(this->handle, fileHeader.data(), (DWORD)fileHeader.size(), &writtenLen, NULL)) {
				return false;
			}
		}
	
		string cFileName = strConv.to_bytes(wstring(FileName));
		this->fileIv = *fileIv = encfs.decodeFileIv(cFileName, fileHeader);
		this->fileIvAvailable = true;
		return true;
	}

	int32_t EncFSFile::read(const LPCWSTR FileName, char* buff, size_t off, DWORD len) {
		lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);

		if (!this->canRead) {
			return -1;
		}

		try {
			int64_t fileIv;
			if (!this->getFileIV(FileName, &fileIv, false)) {
				return 0;
			}

			//printf("read %d %d %d %d\n", fileIv, this->lastBlockNum, off, len);

			// 読み込むブロックの範囲を特定
			size_t blockHeaderSize = encfs.getHeaderSize();
			size_t blockDataSize = encfs.getBlockSize() - blockHeaderSize;
			size_t shift = off % blockDataSize;
			size_t blockNum = off / blockDataSize;
			size_t lastBlockNum = (off + len - 1) / blockDataSize;

			int32_t copiedLen = 0;
			// 直前に読み込んだブロックを利用
			if (blockNum == this->lastBlockNum) {
				uint32_t blockLen = (uint32_t)(this->decodeBuffer.size() - shift);
				if (blockLen > len) {
					blockLen = len;
				}
				memcpy(buff, this->decodeBuffer.data() + shift, blockLen);
				shift = 0;
				len -= blockLen;
				copiedLen += blockLen;
				++blockNum;
				if (len <= 0) {
					return copiedLen;
				}
			}

			size_t blocksOffset = blockNum * encfs.getBlockSize();
			size_t blocksLength = (lastBlockNum + 1) * encfs.getBlockSize() - blocksOffset;
			blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;

			if (blocksLength) {
				// 読み込み開始ブロックに移動
				LARGE_INTEGER distanceToMove;
				distanceToMove.QuadPart = blocksOffset;
				if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
					return -1;
				}

				// 読み込み
				DWORD readLen;
				this->blockBuffer.resize(blocksLength);
				if (!ReadFile(this->handle, &this->blockBuffer[0], (DWORD)blocksLength, &readLen, NULL)) {
					return -1;

				}

				//printf("read2 %d %d %d %d %d\n", shift, blockNum, lastBlockNum, blocksLength, readLen);

				if (readLen > blockHeaderSize + shift) {
					for (size_t i = 0; i < readLen && len > 0; i += encfs.getBlockSize()) {
						size_t blockLen = (readLen - i) > encfs.getBlockSize() ? encfs.getBlockSize() : (readLen - i);

						this->encodeBuffer.assign((const char*)&this->blockBuffer[i], blockLen);
						this->decodeBuffer.clear();
						encfs.decodeBlock(fileIv, this->lastBlockNum = blockNum, this->encodeBuffer, this->decodeBuffer);

						blockLen = this->decodeBuffer.size() - shift;
						if (blockLen > len) {
							blockLen = len;
						}
						memcpy(buff + copiedLen, this->decodeBuffer.data() + shift, blockLen);
						shift = 0;
						len -= (DWORD)blockLen;
						copiedLen += (int32_t)blockLen;
						blockNum++;
					}
				}
			}
			//printf("readEnd %d\n", copiedLen);
			return copiedLen;
		}
		catch (const EncFSInvalidBlockException &ex) {
			return -1;
		}
	}

	int32_t EncFSFile::write(const LPCWSTR FileName, size_t fileSize, const char* buff, size_t off, DWORD len) {
		lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);

		try {
			int64_t fileIv;
			if (!this->getFileIV(FileName, &fileIv, true)) {
				return -1;
			}
			//printf("write %d %d %d %d\n", fileSize, fileIv, off, len);

			// 書き込み先がファイルより大きい場合
			if (off > fileSize) {
				if (!this->_setLength(FileName, fileSize, off)) {
					return -1;
				}
			}

			// 書き込むブロックの範囲を特定
			size_t blockHeaderSize = encfs.getHeaderSize();
			size_t blockDataSize = encfs.getBlockSize() - blockHeaderSize;
			size_t shift = off % blockDataSize;
			size_t blockNum = off / blockDataSize;
			size_t lastBlockNum = (off + len - 1) / blockDataSize;
			size_t blocksOffset = blockNum * encfs.getBlockSize();
			size_t blocksLength = (lastBlockNum + 1) * encfs.getBlockSize() - blocksOffset;
			blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;

			// 書き込み開始ブロックに移動
			LARGE_INTEGER distanceToMove;
			distanceToMove.QuadPart = blocksOffset;
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return -1;
			}

			// ブロックの一部に書き込む場合
			if (shift != 0) {
				//printf("write2 %d %d %d %d\n", blockNum, this->lastBlockNum, shift, this->decodeBuffer.size());
				if (blockNum != this->lastBlockNum) {
					DWORD readLen;
					this->encodeBuffer.resize(encfs.getBlockSize());
					if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
						return -1;
					}
					if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
						return -1;
					}
					this->encodeBuffer.resize(readLen);
					this->decodeBuffer.clear();
					encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
				}
				if (this->decodeBuffer.size() < shift) {
					this->decodeBuffer.append(shift - this->decodeBuffer.size(), (char)0);
				}
			}

			size_t blockDataLen = 0;
			for (size_t i = 0; i < len; i += blockDataLen) {
				blockDataLen = (len - i) > blockDataSize - shift ? blockDataSize - shift : (len - i);
				if (shift != 0) {
					if (this->decodeBuffer.size() < shift + blockDataLen) {
						this->decodeBuffer.resize(shift + blockDataLen);
					}
					memcpy(&this->decodeBuffer[shift], buff, blockDataLen);
					//printf("A %d\n", this->decodeBuffer.size());
				}
				else if (blockDataLen == blockDataSize || off + i + blockDataLen >= fileSize) {
					this->decodeBuffer.assign(buff + i, blockDataLen);
				}
				else {
					DWORD readLen;
					this->encodeBuffer.resize(encfs.getBlockSize());
					if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
						return -1;
					}
					distanceToMove.QuadPart = -(LONGLONG)readLen;
					if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_CURRENT)) {
						return -1;
					}
					this->encodeBuffer.resize(readLen);
					this->decodeBuffer.clear();
					encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
					//printf("B %d %d\n", this->decodeBuffer.size(), readLen);
					memcpy(&this->decodeBuffer[0], buff + i, blockDataLen);
				}
				this->encodeBuffer.clear();
				encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);
				DWORD writtenLen;
				if (!WriteFile(this->handle, this->encodeBuffer.data(), (DWORD)this->encodeBuffer.size(), &writtenLen, NULL)) {
					return -1;
				}
				//printf("%d %d %d\n", blockNum, blockDataLen, writtenLen);
				blockNum++;
				shift = 0;
			}
			//printf("written %d\n", len);
			return len;
		}
		catch (const EncFSInvalidBlockException &ex) {
			return -1;
		}

	}

	bool EncFSFile::flush() {
		return FlushFileBuffers(this->handle);
	}

	bool EncFSFile::setLength(const LPCWSTR FileName, const size_t length) {
		lock_guard<decltype(this->mutexLock)> lock(this->mutexLock);

		LARGE_INTEGER encodedFileSize;
		if (!GetFileSizeEx(this->handle, &encodedFileSize)) {
			return false;
		}
		size_t fileSize = encfs.toDecodedLength(encodedFileSize.QuadPart);

		return this->_setLength(FileName, fileSize, length);
	}

	bool EncFSFile::_setLength(const LPCWSTR FileName, const size_t fileSize, const size_t length) {
		//printf("setLength %d\n", length);

		if (length == 0) {
			LARGE_INTEGER offset;
			offset.QuadPart = 0;
			if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
				return false;
			}
			if (!SetEndOfFile(this->handle)) {
				return false;
			}
			this->fileIvAvailable = false;
			return true;
		}

		// 境界部分をデコード
		size_t blockHeaderSize = encfs.getHeaderSize();
		size_t blockDataSize = encfs.getBlockSize() - blockHeaderSize;
		size_t shift = 0;
		size_t blockNum;
		size_t blocksOffset;
		LARGE_INTEGER distanceToMove;
		if (length < fileSize) {
			// 縮小
			shift = length % blockDataSize;
			blockNum = length / blockDataSize;
		}
		else if (length > fileSize) {
			// 拡大
			shift = fileSize % blockDataSize;
			blockNum = fileSize / blockDataSize;
		}
		int64_t fileIv;
		if (!this->getFileIV(FileName, &fileIv, true)) {
			return false;
		}
		if (shift != 0) {
			blocksOffset = blockNum * encfs.getBlockSize();
			blocksOffset += EncFS::EncFSVolume::HEADER_SIZE;
			DWORD readLen;
			this->encodeBuffer.resize(encfs.getBlockSize());
			distanceToMove.QuadPart = blocksOffset;
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return false;
			}
			if (!ReadFile(this->handle, &this->encodeBuffer[0], (DWORD)this->encodeBuffer.size(), &readLen, NULL)) {
				return false;
			}
			this->encodeBuffer.resize(readLen);
			this->decodeBuffer.clear();
			encfs.decodeBlock(fileIv, blockNum, this->encodeBuffer, this->decodeBuffer);
		}

		size_t encodedLength = encfs.toEncodedLength(length);
		LARGE_INTEGER offset;
		offset.QuadPart = encodedLength;
		if (!SetFilePointerEx(this->handle, offset, NULL, FILE_BEGIN)) {
			return false;
		}
		if (!SetEndOfFile(this->handle)) {
			return false;
		}
		this->lastBlockNum = -1;

		// 境界部分をエンコード
		if (shift != 0) {
			size_t blockDataLen = length - blockNum * blockDataSize;
			if (blockDataLen > blockDataSize) {
				blockDataLen = blockDataSize;
			}
			if (this->decodeBuffer.size() < blockDataLen) {
				this->decodeBuffer.append(blockDataLen - this->decodeBuffer.size(), (char)0);
			}
			else {
				this->decodeBuffer.resize(blockDataLen);
			}
			this->encodeBuffer.clear();
			encfs.encodeBlock(fileIv, this->lastBlockNum = blockNum, this->decodeBuffer, this->encodeBuffer);
			if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
				return false;
			}
			DWORD writtenLen;
			if (!WriteFile(this->handle, this->encodeBuffer.data(), (DWORD)this->encodeBuffer.size(), &writtenLen, NULL)) {
				return false;
			}
		}

		// 拡大した末尾をエンコード
		if (length > fileSize) {
			shift = length % blockDataSize;
			if (shift != 0 && blockNum != length / blockDataSize) {
				blockNum = length / blockDataSize;
				this->decodeBuffer.assign(shift, (char)0);
				this->encodeBuffer.clear();
				encfs.encodeBlock(fileIv, blockNum, this->decodeBuffer, this->encodeBuffer);
				distanceToMove.QuadPart = -(int64_t)shift - (int64_t)blockHeaderSize;
				if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_END)) {
					return false;
				}
				DWORD writtenLen;
				if (!WriteFile(this->handle, this->encodeBuffer.data(), (DWORD)this->encodeBuffer.size(), &writtenLen, NULL)) {
					return false;
				}
			}
		}
		return true;
	}

	bool EncFSFile::changeFileIV(const LPCWSTR FileName, const LPCWSTR NewFileName) {
		int64_t fileIv;
		//printf("changeFileIV\n");
		if (!this->getFileIV(FileName, &fileIv, false)) {
			return true;
		}
		//printf("changeFileIV A %d\n", fileIv);
		string cNewFileName = strConv.to_bytes(wstring(NewFileName));
		string encodedFileHeader;
		encfs.encodeFileIv(cNewFileName, fileIv, encodedFileHeader);
		LARGE_INTEGER distanceToMove;
		distanceToMove.QuadPart = 0;
		if (!SetFilePointerEx(this->handle, distanceToMove, NULL, FILE_BEGIN)) {
			return false;
		}
		//printf("changeFileIV B %d\n", fileIv);
		DWORD writtenLen;
		if (!WriteFile(this->handle, encodedFileHeader.data(), (DWORD)encodedFileHeader.size(), &writtenLen, NULL)) {
			return false;
		}
		//printf("changeFileIV C %d\n", fileIv);
		return true;
	}
}
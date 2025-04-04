/**
 * @file enclaveBase.cc
 * @author Ruilin Wu(202222080631@std.uestc.edu.cn)
 * @brief implement the interface of the base of enclave 
 * @version 0.1
 * @date 2023-12-28
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#include "../../include/enclaveBase.h"


struct Param
{
    int thread_id;
    unsigned char *ptr;
    EVP_MD_CTX *mdCtx;
    uint8_t *SF;
    EcallCrypto *cryptoObj_;
};

struct GetSFTask{
    int begin{0};
    int end{0};
    int thread_id;
};


/**
 * @brief Construct a new Enclave Base object
 * 
 */
EnclaveBase::EnclaveBase() {
    // store the share parameter
    maxSegmentChunkNum_ = MAX_SEGMENT_SIZE / MIN_CHUNK_SIZE;

    // the new object 
    storageCoreObj_ = new EcallStorageCore();
    cryptoObj_ = new EcallCrypto(CIPHER_TYPE, HASH_TYPE);
    /// 

    Enclave::Logging(myName_.c_str(), "init the EnclaveBase.\n");
}

/**
 * @brief Destroy the Enclave Base object
 * 
 */
EnclaveBase::~EnclaveBase() {
    delete storageCoreObj_;
    delete cryptoObj_;
}

/**
 * @brief identify whether it is the end of a segment
 * 
 * @param chunkHashVal the input chunk hash
 * @param chunkSize the input chunk size
 * @param segment the reference to current segment
 * @return true is the end
 * @return false is not the end 
 */
bool EnclaveBase::IsEndOfSegment(uint32_t chunkHashVal, uint32_t chunkSize, 
    Segment_t* segment) {
    // step-1: judge the number of chunks in this segment
    if (segment->chunkNum + 1 > maxSegmentChunkNum_) {
        // exceed the max allow number of chunks
        // start to process
        return true;
    }

    // step-2: judge the segment size
    if (segment->segmentSize + chunkSize < MIN_SEGMENT_SIZE) {
        // continue
        return false;
    } else if (segment->segmentSize + chunkSize > MAX_SEGMENT_SIZE) {
        // capping the size
        return true;
    } else {
        if (chunkHashVal % DIVISOR == PATTERN) {
            // capping the size
            return true;
        } else {
            // continue
            return false;
        }
    }
}

/**
 * @brief convert hash to a value
 * 
 * @param inputHash the input chunk hash
 * @return uint32_t the returned value
 */
uint32_t EnclaveBase::ConvertHashToValue(const uint8_t* inputHash) {
    uint32_t hashVal = 0;
    for (size_t i = 0; i < CHUNK_HASH_SIZE; i++) {
        hashVal += inputHash[i];
    }
    return hashVal;
}

/**
 * @brief update the file recipe
 * 
 * @param chunkAddrStr the chunk address string
 * @param inRecipe the in-enclave recipe buffer
 * @param upOutSGX the upload out-enclave var
 */
void EnclaveBase::UpdateFileRecipe(string& chunkAddrStr, Recipe_t* inRecipe,
    UpOutSGX_t* upOutSGX, uint8_t *FP) {
    memcpy(inRecipe->entryFpList + inRecipe->recipeNum * CHUNK_HASH_SIZE,
           FP, CHUNK_HASH_SIZE);
    inRecipe->recipeNum++;

    if ((inRecipe->recipeNum % Enclave::sendRecipeBatchSize_) == 0) {

        // in-enclave info
        EnclaveClient *sgxClient = (EnclaveClient *)upOutSGX->sgxClient;
        EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
        uint8_t *masterKey = sgxClient->_masterKey;

        // out-enclave info
        Recipe_t *outRecipe = (Recipe_t *)upOutSGX->outRecipe;

        // start to encrypt the file recipe with the enclave key
        //Enclave::Logging("DEBUG", "recipeNum is %d\n", inRecipe->recipeNum);
        cryptoObj_->EncryptWithKey(cipherCtx, inRecipe->entryFpList,
                                inRecipe->recipeNum * CHUNK_HASH_SIZE, masterKey,
                                outRecipe->entryFpList);
        outRecipe->recipeNum = inRecipe->recipeNum;
        // wj: debug
        //Enclave::Logging("DEBUG","invoke Ocall to update file recipe now\n");
        Ocall_UpdateFileRecipe(upOutSGX->outClient);
        _Inline_Ocall++;
        _Inline_RecipeOcall++;
        //memset(inRecipe->entryFpList, 0, Enclave::sendRecipeBatchSize_);
        inRecipe->recipeNum = 0;
    }
    return;
}

/**
 * @brief process an unique chunk
 * 
 * @param chunkAddr the chunk address
 * @param chunkBuffer the chunk buffer
 * @param chunkSize the chunk size
 * @param upOutSGX the upload out-enclave var
 */
void EnclaveBase::ProcessUniqueChunk(RecipeEntry_t* chunkAddr, uint8_t* chunkBuffer, 
    uint32_t chunkSize, UpOutSGX_t* upOutSGX) {
    EnclaveClient* sgxClient = (EnclaveClient*)upOutSGX->sgxClient;
    uint8_t* currentIV = sgxClient->PickNewIV();
    EVP_CIPHER_CTX* cipher = sgxClient->_cipherCtx;
    uint8_t tmpCompressedChunk[MAX_CHUNK_SIZE];
    int tmpCompressedChunkSize = 0;

    uint8_t tmpCipherChunk[MAX_CHUNK_SIZE];
#if (SGX_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_startTime);
#endif
    tmpCompressedChunkSize = LZ4_compress_fast((char*)(chunkBuffer), (char*)tmpCompressedChunk,chunkSize, chunkSize, 3);

#if (SGX_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_endTime);
    _compressTime += (_endTime - _startTime);
    _compressCount++;
#endif
    
#if (SGX_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_startTime);
#endif 
    if (tmpCompressedChunkSize > 0) {
        // it can be compressed

        _compressedDataSize += tmpCompressedChunkSize;

        _onlineCompress_size += tmpCompressedChunkSize;
        
        _onlineBackupSize += tmpCompressedChunkSize;

        // do encryption
        cryptoObj_->EncryptWithKeyIV(cipher, tmpCompressedChunk, tmpCompressedChunkSize,
            Enclave::enclaveKey_, tmpCipherChunk, currentIV);
    } else {
        // it cannot be compressed
        _compressedDataSize += chunkSize;

        _onlineCompress_size += chunkSize;

        _onlineBackupSize += chunkSize;

        tmpCompressedChunkSize = chunkSize;

        // do encryption
        cryptoObj_->EncryptWithKeyIV(cipher, chunkBuffer, chunkSize, Enclave::enclaveKey_,
            tmpCipherChunk, currentIV);
    }
#if (SGX_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_endTime);
    _encryptTime += (_endTime - _startTime);
    _encryptCount++; 
#endif

    // finish the encryption, assign this a container
    storageCoreObj_->SaveChunk((char*)tmpCipherChunk, tmpCompressedChunkSize, 
        chunkAddr, upOutSGX);
    return ;
}


/**
 * @brief update the index store
 * 
 * @param key the key of the k-v pair 
 * @param buffer the data buffer 
 * @param bufferSize the size of the buffer
 * @return true success
 * @return false fail
 */
bool EnclaveBase::UpdateIndexStore(const string& key, const char* buffer, 
    size_t bufferSize) {
    bool status;
    
    Ocall_UpdateIndexStoreBuffer(&status, key.c_str(), key.size(), 
        (const uint8_t*)buffer, bufferSize);

    //Enclave::Logging("DEBUG","update index fp\n");
    return status;
}

bool  EnclaveBase::UpdateIndexSF(const string& key, const char* buffer, 
    size_t bufferSize) {
    bool status;
    Ocall_UpdateIndexStoreSF(&status, key.c_str(), key.size(), 
        (const uint8_t*)buffer, bufferSize);
    
    //Enclave::Logging("DEBUG","update index sf\n");
    return status;
}

/**
 * @brief read the information from the index store
 * 
 * @param key key 
 * @param value value
 * @param upOutSGX the upload out-enclave var
 * @return true 
 * @return false 
 */
bool EnclaveBase::ReadIndexStore(const string& key, string& value,
    UpOutSGX_t* upOutSGX) {
    bool status;
    size_t expectedBufferSize = 0;
    uint8_t* bufferPtr;
    Ocall_ReadIndexStore(&status, key.c_str(), key.size(), &bufferPtr,
        &expectedBufferSize, upOutSGX->outClient);
    
    // copy the buffer to the string
    value.assign((const char*)bufferPtr, expectedBufferSize);
    return status;
}

/**
 * @brief Get the Time Differ object
 * 
 * @param sTime the start time
 * @param eTime the end time
 * @return double the diff of time 
 */
double EnclaveBase::GetTimeDiffer(uint64_t sTime, uint64_t eTime) {
    double second = (eTime - sTime) / SEC_TO_USEC;
    return second;
}

/**
 * @brief reset the value of current segment
 * 
 * @param sgxClient the to the current client
 */
void EnclaveBase::ResetCurrentSegment(EnclaveClient* sgxClient) {
    sgxClient->_segment.chunkNum = 0;
    sgxClient->_segment.minHashVal = UINT32_MAX;
    sgxClient->_segment.segmentSize = 0;
    return ;
}

void EnclaveBase::ProcessSheUniqueChunk(RecipeEntry_t* chunkAddr, uint8_t* chunkBuffer, 
    uint32_t chunkSize, UpOutSGX_t* upOutSGX, uint8_t* chunksf, uint8_t* chunkfp) {
    EnclaveClient* sgxClient = (EnclaveClient*)upOutSGX->sgxClient;
    uint8_t* currentIV = sgxClient->PickNewIV();
    EVP_CIPHER_CTX* cipher = sgxClient->_cipherCtx;
    uint8_t tmpCompressedChunk[MAX_CHUNK_SIZE];
    int tmpCompressedChunkSize = -1;

    uint8_t tmpCipherChunk[MAX_CHUNK_SIZE];


    if(chunkAddr->deltaFlag==NO_DELTA){ //deltaFlag = false 说明这是一个uniquechunk
#if (EDR_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_startTime);
#endif
        tmpCompressedChunkSize = LZ4_compress_fast((char*)(chunkBuffer), (char*)tmpCompressedChunk,chunkSize, chunkSize, 3);
        
#if (EDR_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_endTime);
    _lz4compressTime += (_endTime - _startTime);
    _lz4compressCount++;
#endif
    }else{
        //_sum_Deltasize +=chunkSize;
    }
    

    

    if (tmpCompressedChunkSize > 0) {
        // it can be compressed
        _compressedDataSize += tmpCompressedChunkSize;

        _onlineCompress_size += tmpCompressedChunkSize;
        
        _onlineBackupSize += tmpCompressedChunkSize;

        
#if (EDR_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_startTime);
#endif

        // do encryption
        cryptoObj_->EncryptWithKeyIV(cipher, tmpCompressedChunk, tmpCompressedChunkSize,
            Enclave::enclaveKey_, tmpCipherChunk, currentIV);

#if (EDR_BREAKDOWN== 1)
    Ocall_GetCurrentTime(&_endTime);
    _encryptTime += (_endTime - _startTime);
    _encryptCount++;
#endif
        //_onlineLz4CompressSize += chunkSize - tmpCompressedChunkSize;
    } else {
        // it cannot be compressed
        _compressedDataSize += chunkSize;

        _onlineCompress_size += chunkSize;

        _onlineBackupSize += chunkSize;

        tmpCompressedChunkSize = chunkSize;

        
        
        // do encryption
#if (EDR_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_startTime);
#endif
        cryptoObj_->EncryptWithKeyIV(cipher, chunkBuffer, chunkSize, Enclave::enclaveKey_,
            tmpCipherChunk, currentIV);

#if (EDR_BREAKDOWN== 1)
    Ocall_GetCurrentTime(&_endTime);
    _encryptTime += (_endTime - _startTime);
    _encryptCount++;
#endif

    }


#if(CONTAINER_SEPARATE == 0)

    storageCoreObj_->SavebaseChunk((char*)tmpCipherChunk, tmpCompressedChunkSize, 
        chunkAddr, upOutSGX, chunksf, chunkfp);

    if(chunkAddr->deltaFalg==false){
        //_sum_lz4size += tmpCompressedChunkSize;
    }
    

#endif

#if(CONTAINER_SEPARATE == 1)

    if(chunkAddr->deltaFlag==NO_DELTA){
        storageCoreObj_->SavebaseChunk((char*)tmpCipherChunk, tmpCompressedChunkSize, 
        chunkAddr, upOutSGX, chunksf, chunkfp);
        
        //_sum_lz4size += tmpCompressedChunkSize;

    }else{
        //_deltaDataSize += tmpCompressedChunkSize;
        storageCoreObj_->SavedeltaChunk((char*)tmpCipherChunk, tmpCompressedChunkSize, 
        chunkAddr, upOutSGX, chunksf, chunkfp);
    }
    
#endif

    return ;
}
/**
 * @file encECall.cpp
 * @author Ruilin Wu(202222080631@std.uestc.edu.cn)
 * @brief define the ECALLs of StoreEnclave 
 * @version 0.1
 * @date 2023-10-02
 * 
 * @copyright Copyright (c) 2020
 * 
 */

#include "../../include/storeECall.h"

using namespace Enclave;

/**
 * @brief init the ecall
 * 
 * @param indexType the type of the index
 */
void Ecall_Init_Upload(int indexType) {
    // choose different types of indexes here
    switch (indexType) {
        
        case FREQ_INDEX: {
            
            enclaveBaseObj_ = new EcallFreqIndex();
            // Enclave::Logging("Debug", "new enclave base, fp ocall: %u\n", enclaveBaseObj_->_Inline_FPOcall);
            break;
        }

        case SECURE_MEGA: {
            enclaveBaseObj_ = new EcallMeGA();
            break;
        }

        case DEBE: {
            enclaveBaseObj_ = new EcallDEBE();
            break;
        }

        case FORWORD :{
            enclaveBaseObj_ = new EcallFreqIndex();
            enclaveBaseObj_->Forward_Flag = true;
            break;
        }

        case ONLY_FORWORD:{
            enclaveBaseObj_ = new EcallFreqIndex();
            enclaveBaseObj_->Offline_Flag = false;
            break;
        }

        default:
            Ocall_SGX_Exit_Error("wrong enclave index type.");
    }

    size_t readFileSize = 0;
    Ocall_InitReadSealedFile(&readFileSize, ENCLAVE_INDEX_INFO_NAME);
    if (readFileSize != 0) {
        // the file exists, read previous stastics info
        uint8_t* infoBuf = (uint8_t*) malloc(sizeof(uint64_t) * 5);
        size_t offset = 0;
        Ocall_ReadSealedData(ENCLAVE_INDEX_INFO_NAME, infoBuf, 
            sizeof(uint64_t) * 5);
        memcpy(&enclaveBaseObj_->_logicalDataSize, infoBuf + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(&enclaveBaseObj_->_logicalChunkNum, infoBuf + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(&enclaveBaseObj_->_uniqueDataSize, infoBuf + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(&enclaveBaseObj_->_uniqueChunkNum, infoBuf + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(&enclaveBaseObj_->_compressedDataSize, infoBuf + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        free(infoBuf);
    }
    Ocall_CloseReadSealedFile(ENCLAVE_INDEX_INFO_NAME);
    return ;
}

/**
 * @brief destore the enclave memory 
 * 
 * @return bool 
 */
void Ecall_Destroy_Upload() {
    // persist the enclave info here
    bool ret;
    Ocall_InitWriteSealedFile(&ret, ENCLAVE_INDEX_INFO_NAME);
    if (ret != true) {
        Ocall_SGX_Exit_Error("cannot open the enclave index info file.");
    } 
    Ocall_WriteSealedData(ENCLAVE_INDEX_INFO_NAME, 
        (uint8_t*)&enclaveBaseObj_->_logicalDataSize, sizeof(uint64_t));
    Ocall_WriteSealedData(ENCLAVE_INDEX_INFO_NAME, 
        (uint8_t*)&enclaveBaseObj_->_logicalChunkNum, sizeof(uint64_t));
    Ocall_WriteSealedData(ENCLAVE_INDEX_INFO_NAME, 
        (uint8_t*)&enclaveBaseObj_->_uniqueDataSize, sizeof(uint64_t));
    Ocall_WriteSealedData(ENCLAVE_INDEX_INFO_NAME, 
        (uint8_t*)&enclaveBaseObj_->_uniqueChunkNum, sizeof(uint64_t));
    Ocall_WriteSealedData(ENCLAVE_INDEX_INFO_NAME, 
        (uint8_t*)&enclaveBaseObj_->_compressedDataSize, sizeof(uint64_t));
    Ocall_CloseWriteSealedFile(ENCLAVE_KEY_FILE_NAME);
    if (enclaveBaseObj_) {
        delete enclaveBaseObj_;
    }
    return ;
}

/**
 * @brief process one batch of chunk
 * 
 * @param recvChunkBuf the recv chunk buffer
 * @param upOutSGX the pointer to enclave-needed structure
 */
void Ecall_ProcChunkBatch(SendMsgBuffer_t* recvChunkBuf, UpOutSGX_t* upOutSGX) {
    enclaveBaseObj_->ProcessOneBatch(recvChunkBuf, upOutSGX);
    return ;
}

/**
 * @brief process the tail batch 
 * 
 * @param upOutSGX the pointer to enclave-needed structure 
 */
void Ecall_ProcTailChunkBatch(UpOutSGX_t* upOutSGX) {
    enclaveBaseObj_->ProcessTailBatch(upOutSGX);
    return ;
}

void Ecall_ProcOffline(SendMsgBuffer_t* recvChunkBuf, UpOutSGX_t* upOutSGX){
    enclaveBaseObj_->ProcessOffline(recvChunkBuf, upOutSGX);
    return ; 
}

/**
 * @brief init the inside client var
 * 
 * @param clientID the client ID
 * @param type the index type 
 * @param optType the operation type (upload/download)
 * @param encMasterKey the encrypted master key 
 * @param sgxClient the pointer to the sgx client
 */
void Ecall_Init_Client(uint32_t clientID, int type, int optType, 
    uint8_t* encMasterKey, void** sgxClient) {
    // allocate a client resource inside the enclave
    EnclaveClient* newClient = new EnclaveClient(clientID, type, optType);

{
    // check the session key for this users 
#if (MULTI_CLIENT == 1)
    Enclave::sessionKeyLck_.lock();
#endif
    auto findResult = Enclave::clientSessionKeyIndex_.find(clientID);
    if (findResult == Enclave::clientSessionKeyIndex_.end()) {
        Ocall_SGX_Exit_Error("get the session key fails.");
    } else {
        // can find the session key, directly set the session key of this client
        memcpy(newClient->_sessionKey, &findResult->second[0],
            CHUNK_HASH_SIZE);
    }
#if (MULTI_CLIENT == 1)
    Enclave::sessionKeyLck_.unlock();
#endif
}

    // set the client master key
    newClient->SetMasterKey(encMasterKey, CHUNK_HASH_SIZE);
    // return the sgx client ptr
    memcpy(sgxClient, &newClient, sizeof(EnclaveClient*));
    return ;
}

/**
 * @brief destroy the inside client var
 * 
 * @param sgxClient the sgx-client ptr
 */
void Ecall_Destroy_Client(void* sgxClient) {
    // destroy the client resource inside the enclave
    EnclaveClient* sgxClientPtr = (EnclaveClient*)sgxClient;
    delete sgxClientPtr;
    return ;
}

/**
 * @brief init the enclave 
 * 
 * @param enclaveConfig the enclave config
 */
void Ecall_Enclave_Init(EnclaveConfig_t* enclaveConfig) {
    using namespace Enclave;
    // set the enclave key and index query key (this can be configured by RA)
    enclaveKey_ = (uint8_t*) malloc(sizeof(uint8_t) * CHUNK_HASH_SIZE);
    indexQueryKey_ = (uint8_t*) malloc(sizeof(uint8_t) * CHUNK_HASH_SIZE);
    firstBootstrap_ = true;

    // config
    sendChunkBatchSize_ = enclaveConfig->sendChunkBatchSize;
    sendRecipeBatchSize_ = enclaveConfig->sendRecipeBatchSize;
    topKParam_ = enclaveConfig->topKParam;

    // check the file 
    size_t readFileSize = 0;
    Ocall_InitReadSealedFile(&readFileSize, ENCLAVE_KEY_FILE_NAME);
    if (readFileSize == 0) {
        // file not exists
        firstBootstrap_ = true;

        // randomly generate the data key, query key, and the global secret
        sgx_read_rand(enclaveKey_, CHUNK_HASH_SIZE);
        sgx_read_rand(indexQueryKey_, CHUNK_HASH_SIZE);
    } else {
        // file exists
        firstBootstrap_ = false;
        uint8_t* readBuffer = (uint8_t*) malloc(CHUNK_HASH_SIZE * 2);
        Ocall_ReadSealedData(ENCLAVE_KEY_FILE_NAME, readBuffer, CHUNK_HASH_SIZE * 2);
        memcpy(enclaveKey_, readBuffer, CHUNK_HASH_SIZE);
        memcpy(indexQueryKey_, readBuffer + CHUNK_HASH_SIZE, 
            CHUNK_HASH_SIZE);
        free(readBuffer);
    }
    Ocall_CloseReadSealedFile(ENCLAVE_KEY_FILE_NAME);
    return ;
}

/**
 * @brief destroy the enclave
 * 
 */
void Ecall_Enclave_Destroy() {
    using namespace Enclave;
    // check the status 
    if (firstBootstrap_ == true) {
        // perisit the key
        bool ret;
        Ocall_InitWriteSealedFile(&ret, ENCLAVE_KEY_FILE_NAME);
        if (ret != true) {
            Ocall_SGX_Exit_Error("cannot open the enclave key file.");
        } 
        Ocall_WriteSealedData(ENCLAVE_KEY_FILE_NAME, enclaveKey_, 
            CHUNK_HASH_SIZE);
        Ocall_WriteSealedData(ENCLAVE_KEY_FILE_NAME, indexQueryKey_,
            CHUNK_HASH_SIZE);
        Ocall_CloseWriteSealedFile(ENCLAVE_KEY_FILE_NAME);
    }
    // free the enclave key, index query key and the global secret
    free(enclaveKey_); 
    free(indexQueryKey_);
    return ;
}

/**
 * @brief get the enclave info 
 * 
 * @param info the enclave info
 */
void Ecall_GetEnclaveInfo(EnclaveInfo_t* info) {
    info->logicalDataSize = enclaveBaseObj_->_logicalDataSize;
    info->logicalChunkNum = enclaveBaseObj_->_logicalChunkNum;
    info->uniqueDataSize = enclaveBaseObj_->_uniqueDataSize;
    info->uniqueChunkNum = enclaveBaseObj_->_uniqueChunkNum;
    info->compressedSize = enclaveBaseObj_->_compressedDataSize;
    info->onlineCompress_size = enclaveBaseObj_->_onlineCompress_size;
    info->Online_DeltaSaveSize = enclaveBaseObj_->_Online_DeltaSaveSize;
    info->inline_Ocall = enclaveBaseObj_->_Inline_Ocall;
    info->_Inline_FPOcall =     enclaveBaseObj_->_Inline_FPOcall;
    info->_Inline_SFOcall =     enclaveBaseObj_->_Inline_SFOcall;
    info->_Inline_LocalOcall =  enclaveBaseObj_->_Inline_LocalOcall;
    info->_Inline_LoadOcall =   enclaveBaseObj_->_Inline_LoadOcall;
    info->_Inline_DeltaOcall =  enclaveBaseObj_->_Inline_DeltaOcall;
    info->_Inline_RecipeOcall = enclaveBaseObj_->_Inline_RecipeOcall;
    info->_inline_have_similar_chunk_num = enclaveBaseObj_->_inline_have_similar_chunk_num;
    info->_inline_need_load_container_num = enclaveBaseObj_->_inline_need_load_container_num;
    info->_inlineDeltaChunkNum = enclaveBaseObj_->_inlineDeltaChunkNum;
    // info->_inline_average_similarity = enclaveBaseObj_->_inline_total_similarity / enclaveBaseObj_->_inline_batch_num * 1.0;

#if(EDR_BREAKDOWN == 1)
    double rawOcallTime = enclaveBaseObj_->_testOCallTime / 
        static_cast<double>(enclaveBaseObj_->_testOCallCount);
    double MS_TO_USEC = 1000.0;


    info->dataTranTime = (enclaveBaseObj_->_dataTransTime - 
        (enclaveBaseObj_->_dataTransCount * rawOcallTime)) / MS_TO_USEC;
    info->fpTime = (enclaveBaseObj_->_fingerprintTime - 
        (enclaveBaseObj_->_fingerprintCount * rawOcallTime)) / MS_TO_USEC;
    info->freqTime = (enclaveBaseObj_->_freqTime -
        (enclaveBaseObj_->_freqCount * rawOcallTime)) / MS_TO_USEC;
    info->firstDedupTime = (enclaveBaseObj_->_firstDedupTime - 
        (enclaveBaseObj_->_firstDedupCount * rawOcallTime)) / MS_TO_USEC;
    info->secondDedupTime = (enclaveBaseObj_->_secondDedupTime - 
        (enclaveBaseObj_->_secondDedupCount * rawOcallTime)) / MS_TO_USEC;
    info->lz4compressTime = (enclaveBaseObj_->_lz4compressTime - 
        (enclaveBaseObj_->_lz4compressCount * rawOcallTime)) / MS_TO_USEC;
    info->encTime = (enclaveBaseObj_->_encryptTime - 
        (enclaveBaseObj_->_encryptCount * rawOcallTime)) / MS_TO_USEC;

    info->sfTime = (enclaveBaseObj_->_superfeatureTime - 
        (enclaveBaseObj_->_superfeatureCount * rawOcallTime)) / MS_TO_USEC;

    info->checkTime = (enclaveBaseObj_->_localcheckerTime - 
        (enclaveBaseObj_->_localcheckerCount * rawOcallTime)) / MS_TO_USEC;
    
    info->deltacompressTime = (enclaveBaseObj_->_deltacompressTime - 
        (enclaveBaseObj_->_deltacompressCount * rawOcallTime)) / MS_TO_USEC;

    
/*     // reset count
    enclaveBaseObj_->_testOCallCount = 0;
    enclaveBaseObj_->_dataTransCount = 0;
    enclaveBaseObj_->_fingerprintCount = 0;
    enclaveBaseObj_->_freqCount = 0;
    enclaveBaseObj_->_firstDedupCount = 0;
    enclaveBaseObj_->_secondDedupCount = 0;
    enclaveBaseObj_->_lz4compressCount = 0;
    enclaveBaseObj_->_encryptCount = 0;
    enclaveBaseObj_->_superfeatureCount = 0;
    enclaveBaseObj_->_localcheckerCount = 0;
    enclaveBaseObj_->_deltacompressCount = 0;

    // reset time
    enclaveBaseObj_->_testOCallTime = 0;
    enclaveBaseObj_->_dataTransTime = 0;
    enclaveBaseObj_->_fingerprintTime = 0;
    enclaveBaseObj_->_freqTime = 0;
    enclaveBaseObj_->_firstDedupTime = 0;
    enclaveBaseObj_->_secondDedupTime = 0;
    enclaveBaseObj_->_lz4compressTime = 0;
    enclaveBaseObj_->_encryptTime = 0;
    enclaveBaseObj_->_superfeatureTime = 0;
    enclaveBaseObj_->_localcheckerTime = 0;
    enclaveBaseObj_->_deltacompressTime = 0; */

#endif

#if (SGX_BREAKDOWN == 1)
    double rawOcallTime = enclaveBaseObj_->_testOCallTime / 
        static_cast<double>(enclaveBaseObj_->_testOCallCount);
    double MS_TO_USEC = 1000.0;
    info->dataTranTime = (enclaveBaseObj_->_dataTransTime - 
        (enclaveBaseObj_->_dataTransCount * rawOcallTime)) / MS_TO_USEC;
    info->fpTime = (enclaveBaseObj_->_fingerprintTime - 
        (enclaveBaseObj_->_fingerprintCount * rawOcallTime)) / MS_TO_USEC;
    info->freqTime = (enclaveBaseObj_->_freqTime -
        (enclaveBaseObj_->_freqCount * rawOcallTime)) / MS_TO_USEC;
    info->firstDedupTime = (enclaveBaseObj_->_firstDedupTime - 
        (enclaveBaseObj_->_firstDedupCount * rawOcallTime)) / MS_TO_USEC;
    info->secondDedupTime = (enclaveBaseObj_->_secondDedupTime - 
        (enclaveBaseObj_->_secondDedupCount * rawOcallTime)) / MS_TO_USEC;
    info->compTime = (enclaveBaseObj_->_compressTime - 
        (enclaveBaseObj_->_compressCount * rawOcallTime)) / MS_TO_USEC;
    info->encTime = (enclaveBaseObj_->_encryptTime - 
        (enclaveBaseObj_->_encryptCount * rawOcallTime)) / MS_TO_USEC;
    
    // reset count
    enclaveBaseObj_->_testOCallCount = 0;
    enclaveBaseObj_->_dataTransCount = 0;
    enclaveBaseObj_->_fingerprintCount = 0;
    enclaveBaseObj_->_freqCount = 0;
    enclaveBaseObj_->_firstDedupCount = 0;
    enclaveBaseObj_->_secondDedupCount = 0;
    enclaveBaseObj_->_compressCount = 0;
    enclaveBaseObj_->_encryptCount = 0;

    // reset time
    enclaveBaseObj_->_testOCallTime = 0;
    enclaveBaseObj_->_dataTransTime = 0;
    enclaveBaseObj_->_fingerprintTime = 0;
    enclaveBaseObj_->_freqTime = 0;
    enclaveBaseObj_->_firstDedupTime = 0;
    enclaveBaseObj_->_secondDedupTime = 0;
    enclaveBaseObj_->_compressTime = 0;
    enclaveBaseObj_->_encryptTime = 0;
#endif
    return ;
}

void Ecall_GetOfflineInfo(EnclaveInfo_t* info){
    info->offlineCompress_size = enclaveBaseObj_->offlinebackOBj_->_offlineCompress_size;
    info->offlineDeletenum = enclaveBaseObj_->offlinebackOBj_->_offlineDeletenum;
    info->offlineDeltanum = enclaveBaseObj_->offlinebackOBj_->_offlineDeltanum;
    info->offlineDeDeltanum = enclaveBaseObj_->offlinebackOBj_->_offlineDeDeltanum;
    info->baseChunkNum = enclaveBaseObj_->offlinebackOBj_->_baseChunkNum;
    info->baseDataSize = enclaveBaseObj_->offlinebackOBj_->_baseDataSize;
    info->deltaChunkNum = enclaveBaseObj_->offlinebackOBj_->_deltaChunkNum;
    info->deltaDataSize = enclaveBaseObj_->offlinebackOBj_->_deltaDataSize;
    info->Offline_DeltaSaveSize = enclaveBaseObj_->offlinebackOBj_->_Offline_DeltaSaveSize;
    info->OfflinedeltaChunkNum = enclaveBaseObj_->offlinebackOBj_->_offlinedeltaChunkNum;
    info->lz4SaveSize = enclaveBaseObj_->offlinebackOBj_->_lz4SaveSize;
    info->DeltaSaveSize = enclaveBaseObj_->offlinebackOBj_->_DeltaSaveSize;
    info->offline_Ocall = enclaveBaseObj_->offlinebackOBj_->_offline_Ocall;
    
}

void Ecall_UpdateOnlineInfo(){
    enclaveBaseObj_->_onlineCompress_size = enclaveBaseObj_->offlinebackOBj_->_offlineCompress_size;
    enclaveBaseObj_->_onlineBackupSize = 0;
    enclaveBaseObj_->_baseChunkNum = enclaveBaseObj_->offlinebackOBj_->_baseChunkNum;
    enclaveBaseObj_->_baseDataSize = enclaveBaseObj_->offlinebackOBj_->_baseDataSize;
    enclaveBaseObj_->_deltaChunkNum = enclaveBaseObj_->offlinebackOBj_->_deltaChunkNum;
    enclaveBaseObj_->_deltaDataSize = enclaveBaseObj_->offlinebackOBj_->_deltaDataSize;
    enclaveBaseObj_->_lz4SaveSize = enclaveBaseObj_->offlinebackOBj_->_lz4SaveSize;
    enclaveBaseObj_->_DeltaSaveSize = enclaveBaseObj_->offlinebackOBj_->_DeltaSaveSize;
}
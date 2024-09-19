/**
 * @file ecallFreqTwoIndex.cc
 * @author Ruilin Wu(202222080631@std.uestc.edu.cn)
 * @brief implement the interface of frequency-two index
 * @version 0.1
 * @date 2023-01-15
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include "../../include/ecallMeGA.h"




#define INIT(L, R, OFF) L = 0x6c078965 * ((R) ^ (R >> 30u)) + (OFF)


/**
 * @brief Construct a new Ecall Frequency Index object
 * 
 */
EcallMeGA::EcallMeGA() {
    topThreshold_ = Enclave::topKParam_;
    insideDedupIndex_ = new EcallEntryHeap();
    insideDedupIndex_->SetHeapSize(topThreshold_);
    cmSketch_ = new EcallCMSketch(sketchWidth_, sketchDepth_);
    InContainercache_ = new InContainercache();
    // temp_iv = (uint8_t *)malloc(CRYPTO_BLOCK_SIZE);
    // temp_chunkbuffer = (uint8_t *)malloc(MAX_CHUNK_SIZE);
    // basechunkbuffer = (uint8_t *)malloc(MAX_CHUNK_SIZE);

    offlinebackOBj_ = new OFFLineBackward();

    if (ENABLE_SEALING) {
        if (!this->LoadDedupIndex()) {
            Enclave::Logging(myName_.c_str(), "do not need to load the index.\n");
        }
    }
    Enclave::Logging(myName_.c_str(), "init the EcallMeGA.\n");
}

/**
 * @brief Destroy the Ecall Frequency Index object
 * 
 */
EcallMeGA::~EcallMeGA() {
    // size_t heapSize = insideDedupIndex_->Size();
    // fprintf(stdout, "the number of entry: %lu\n", heapSize);
    // for (size_t i = 0; i < heapSize; i++) {
    //     fprintf(stdout, "%u\n", insideDedupIndex_->TopEntry());
    //     if (insideDedupIndex_->Size() > 1) {
    //         insideDedupIndex_->Pop();
    //     } else {
    //         break;
    //     }
    // }

    if (ENABLE_SEALING) {
        this->PersistDedupIndex();
    }
    delete insideDedupIndex_;
    delete cmSketch_;
    delete InContainercache_;
    delete offlinebackOBj_;
    // free(temp_iv);
    // free(temp_chunkbuffer);
    // free(basechunkbuffer);

    Enclave::Logging(myName_.c_str(), "========EcallMeGA Info========\n");
    Enclave::Logging(myName_.c_str(), "logical chunk num: %lu\n", _logicalChunkNum);
    Enclave::Logging(myName_.c_str(), "logical data size: %lu\n", _logicalDataSize);
    Enclave::Logging(myName_.c_str(), "unique chunk num: %lu\n", _uniqueChunkNum);
    Enclave::Logging(myName_.c_str(), "unique data size: %lu\n", _uniqueDataSize);
    Enclave::Logging(myName_.c_str(), "compressed data size: %lu\n", _compressedDataSize); 
    // Enclave::Logging(myName_.c_str(), "inside dedup chunk num: %lu\n", insideDedupChunkNum_);
    // Enclave::Logging(myName_.c_str(), "inside dedup data size: %lu\n", insideDedupDataSize_);
    Enclave::Logging(myName_.c_str(), "===================================\n");
}

uint32_t* EcallMeGA::mt_n_get(uint32_t seed, int n) {
    //uint32_t *head_arr = (uint32_t *) malloc(sizeof(uint32_t) * (n + 1));
    auto *head_arr = new uint32_t[n + 1];
    auto *last_arr = new uint32_t[n + 1];
    auto *result = new uint32_t[n];
    head_arr[0] = seed;
    for (int i = 1; i < n + 1; i++)
        INIT(head_arr[i], head_arr[i - 1], i);
    uint32_t temp = head_arr[n];
    for (int i = n; i < 397; i++)
        INIT(temp, temp, i + 1);
    last_arr[0] = temp;
    for (int i = 1; i < n + 1; ++i)
        INIT(last_arr[i], last_arr[i - 1], i + 397);
    for (int i = 0; i < n; i++) {
        temp = (head_arr[i] & 0x80000000) + (head_arr[i + 1] & 0x7fffffffu);
        head_arr[i] = (temp >> 1u) ^ last_arr[i];
        if (temp % 2 != 0)
            head_arr[i] = head_arr[i] ^ 0x9908b0df;
    }
    for (int i = 0; i < n;
         ++i) {
        uint32_t y = head_arr[i];
        y = y ^ y >> 11u;
        y = y ^ y << 7u & 2636928640u;
        y = y ^ y << 15u & 4022730752u;
        y = y ^ y >> 18u;
        result[i] = y;
    }
    delete [] head_arr;
    delete [] last_arr;
    return result;
}

void EcallMeGA::getSF2(unsigned char *ptr, EVP_MD_CTX *mdCtx, uint8_t *SF, EcallCrypto *cryptoObj_, int chunkSize) {
    
 
  EVP_MD_CTX *mdCtx_ = mdCtx;
  int BLOCK_SIZE = chunkSize;
  constexpr auto WINDOW_SIZE = 48;
  constexpr auto SF_NUM = 3;
  constexpr auto FEATURE_NUM = 12;
  const auto A = 37u, MOD = 1000000007u;
  uint64_t Apower = 1;  uint32_t TRANSPOSE_M[FEATURE_NUM];
  uint32_t TRANSPOSE_A[FEATURE_NUM];
  int subchunkIndex[FEATURE_NUM + 1];

  uint32_t feature[FEATURE_NUM];
  uint64_t superfeature[SF_NUM];
  subchunkIndex[0] = 0;
  for (int i = 0; i < FEATURE_NUM; ++i) {
    subchunkIndex[i + 1] = (BLOCK_SIZE * (i + 1)) / FEATURE_NUM;
  }
  auto *rds = mt_n_get(922,FEATURE_NUM * 2);
  for (int i = 0; i < FEATURE_NUM; ++i) {
    TRANSPOSE_M[i] = ((rds[i << 1] >> 1) << 1) + 1;
    TRANSPOSE_A[i] = rds[(i <<1) + 1];
  }
  delete[] rds;
  for (int i = 0; i < WINDOW_SIZE - 1; ++i) {
    Apower *= A;
    Apower %= MOD;
  }
  
  memset(feature,0,FEATURE_NUM * sizeof(uint32_t));
  memset(superfeature,0,SF_NUM  *sizeof(uint64_t));

  
  for (int i = 0; i < FEATURE_NUM; ++i) {
    int64_t fp = 0;
    for (int j = subchunkIndex[i]; j < subchunkIndex[i] + WINDOW_SIZE; ++j) {
      fp *= A;
      fp += (unsigned char)ptr[j];
      fp %= MOD;
    }

    for (int j = subchunkIndex[i]; j < subchunkIndex[i + 1] - WINDOW_SIZE + 1;
         ++j) {
      if (fp > feature[i])
        feature[i] = fp;
      fp -= (ptr[j] * Apower) % MOD;
      while (fp < 0)
        fp += MOD;
      if (j != subchunkIndex[i + 1] - WINDOW_SIZE) {
        fp *= A;
        fp += ptr[j + WINDOW_SIZE];
        fp %= MOD;
      }
    }
  }
  
  for (int i = 0; i < FEATURE_NUM / SF_NUM; ++i) {
    std::sort(feature + i * SF_NUM, feature + (i + 1) * SF_NUM);
  }

 
  int offset = 0;
  for (int i = 0; i < SF_NUM; ++i) {
    uint64_t temp[FEATURE_NUM / SF_NUM];
    for (int j = 0; j < FEATURE_NUM / SF_NUM; ++j) {
      temp[j] = feature[j * SF_NUM + i];
    }
    uint8_t temp3[4 * sizeof(uint64_t)];
    memcpy(temp3, temp, 4 * sizeof(uint64_t));
    uint8_t temp2[CHUNK_HASH_SIZE];
    pthread_mutex_lock(&mutex2);
    cryptoObj_->GenerateHash(mdCtx, temp3, sizeof(uint64_t) * FEATURE_NUM / SF_NUM, temp2);
    pthread_mutex_unlock(&mutex2);
    memcpy(SF + offset, temp2, CHUNK_HASH_SIZE);
    offset = offset + CHUNK_HASH_SIZE;
  }
  
  
}

void* EcallMeGA::GetSF_thread_func_f(void* arg){
    auto info = *(GetSFTask*)arg;
    for(int i = info.begin ; i < info.end; i++){
        auto param = param_list[i];
        if(param.SF){
            getSF2(param.ptr,param.mdCtx,param.SF,param.cryptoObj_,param.chunksize);
        }
    }
    return NULL;
}



void EcallMeGA::getSF(unsigned char *ptr, EVP_MD_CTX *mdCtx, uint8_t *SF, EcallCrypto *cryptoObj_)
{
    std::mt19937 gen1, gen2; // 优化
    std::uniform_int_distribution<uint32_t> full_uint32_t;
    EVP_MD_CTX *mdCtx_ = mdCtx;
    int BLOCK_SIZE, WINDOW_SIZE;
    int SF_NUM, FEATURE_NUM;
    uint32_t *TRANSPOSE_M;
    uint32_t *TRANSPOSE_A;
    int *subchunkIndex;
    const uint32_t A = 37, MOD = 1000000007;
    uint64_t Apower = 1;
    uint32_t *feature;
    uint64_t *superfeature;
    gen1 = std::mt19937(922);
    gen2 = std::mt19937(314159);
    full_uint32_t = std::uniform_int_distribution<uint32_t>(std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max());

    BLOCK_SIZE = 8096;
    WINDOW_SIZE = 48;
    SF_NUM = 3;
    FEATURE_NUM = 12;
    TRANSPOSE_M = new uint32_t[FEATURE_NUM];
    TRANSPOSE_A = new uint32_t[FEATURE_NUM];

    feature = new uint32_t[FEATURE_NUM];
    superfeature = new uint64_t[SF_NUM];
    subchunkIndex = new int[FEATURE_NUM + 1];
    subchunkIndex[0] = 0;
    for (int i = 0; i < FEATURE_NUM; ++i)
    {
        subchunkIndex[i + 1] = (BLOCK_SIZE * (i + 1)) / FEATURE_NUM;
    }
    for (int i = 0; i < FEATURE_NUM; ++i)
    {
        TRANSPOSE_M[i] = ((full_uint32_t(gen1) >> 1) << 1) + 1;
        TRANSPOSE_A[i] = full_uint32_t(gen1);
    }
    for (int i = 0; i < WINDOW_SIZE - 1; ++i)
    {
        Apower *= A;
        Apower %= MOD;
    }
    for (int i = 0; i < FEATURE_NUM; ++i)
        feature[i] = 0;
    for (int i = 0; i < SF_NUM; ++i)
        superfeature[i] = 0; // 初始化

    for (int i = 0; i < FEATURE_NUM; ++i)
    {
        int64_t fp = 0;

        for (int j = subchunkIndex[i]; j < subchunkIndex[i] + WINDOW_SIZE; ++j)
        {
            fp *= A;
            fp += (unsigned char)ptr[j];
            fp %= MOD;
        }

        for (int j = subchunkIndex[i]; j < subchunkIndex[i + 1] - WINDOW_SIZE + 1; ++j)
        {
            if (fp > feature[i])
                feature[i] = fp;

            fp -= (ptr[j] * Apower) % MOD;
            while (fp < 0)
                fp += MOD;
            if (j != subchunkIndex[i + 1] - WINDOW_SIZE)
            {
                fp *= A;
                fp += ptr[j + WINDOW_SIZE];
                fp %= MOD;
            }
        }
    }

    for (int i = 0; i < FEATURE_NUM / SF_NUM; ++i)
    {
        std::sort(feature + i * SF_NUM, feature + (i + 1) * SF_NUM);
    }
    int offset = 0;
    for (int i = 0; i < SF_NUM; ++i)
    {
        uint64_t temp[FEATURE_NUM / SF_NUM];
        for (int j = 0; j < FEATURE_NUM / SF_NUM; ++j)
        {
            temp[j] = feature[j * SF_NUM + i];
        }
        uint8_t *temp3;

        temp3 = (uint8_t *)malloc(4 * sizeof(uint64_t));

        memcpy(temp3, temp, 4 * sizeof(uint64_t));

        uint8_t *temp2;
        temp2 = (uint8_t *)malloc(CHUNK_HASH_SIZE);
        cryptoObj_->GenerateHash(mdCtx_, temp3, sizeof(uint64_t) * FEATURE_NUM / SF_NUM, temp2);
        memcpy(SF + offset, temp2, CHUNK_HASH_SIZE);
        offset = offset + CHUNK_HASH_SIZE;
        free(temp2);
        free(temp3);
    }

    delete[] TRANSPOSE_M;
    delete[] TRANSPOSE_A;
    delete[] feature;
    delete[] superfeature;
    delete[] subchunkIndex;
    return;
}



/**
 * @brief update the inside-enclave with only freq
 * 
 * @param ChunkFp the chunk fp
 * @param currentFreq the current frequency
 */
void EcallMeGA::UpdateInsideIndexFreq(const string& chunkFp, uint32_t currentFreq) {
    insideDedupIndex_->Update(chunkFp, currentFreq);
    return ;
}

/**
 * @brief process the tailed batch when received the end of the recipe flag
 * 
 * @param upOutSGX the pointer to enclave-related var
 */
void EcallMeGA::ProcessTailBatch(UpOutSGX_t* upOutSGX) {
    // the in-enclave info
    EnclaveClient *sgxClient = (EnclaveClient *)upOutSGX->sgxClient;
    Recipe_t *inRecipe = &sgxClient->_inRecipe;
    Recipe_t *outRecipe = (Recipe_t *)upOutSGX->outRecipe;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t *masterKey = sgxClient->_masterKey;
    
    //Enclave::Logging("DE BUG","Tail in\n");
    string tmpChunkAddr;
    tmpChunkAddr.resize(sizeof(RecipeEntry_t), 0);

    //clear the delay-cache if it is not empty

    

    if (inRecipe->recipeNum != 0)
    {
        cryptoObj_->EncryptWithKey(cipherCtx, inRecipe->entryFpList,
                                   inRecipe->recipeNum * CHUNK_HASH_SIZE, masterKey,
                                   outRecipe->entryFpList);
        //Enclave::Logging("DEBUG","tail recipeNum is %d\n", inRecipe->recipeNum);
        outRecipe->recipeNum = inRecipe->recipeNum;
        Ocall_UpdateFileRecipe(upOutSGX->outClient);
        inRecipe->recipeNum = 0;
    }
    // clear in-container
    if (sgxClient->_inContainer.curSize != 0)
    {
        memcpy(upOutSGX->curContainer->body, sgxClient->_inContainer.buf,
               sgxClient->_inContainer.curSize);
        upOutSGX->curContainer->currentSize = sgxClient->_inContainer.curSize;
    }
    // clear in_deltacontainer
    if (sgxClient->_deltainContainer.curSize != 0)
    {
        memcpy(upOutSGX->curDeltaContainer->body, sgxClient->_deltainContainer.buf,
               sgxClient->_deltainContainer.curSize);
        upOutSGX->curDeltaContainer->currentSize = sgxClient->_deltainContainer.curSize;
    }

   // Enclave::Logging("DE BUG","Tail down\n");
    return;
}

/**
 * @brief persist the deduplication index into the disk
 * 
 * @return true success
 * @return false fail
 */
bool EcallMeGA::PersistDedupIndex() {
    size_t itemNum;
    bool persistenceStatus;
    size_t requiredBufferSize = 0;
    size_t offset = 0;
    // a pointer to store tmp buffer
    uint8_t* tmpBuffer = NULL;

    // step-1: persist the sketch state
    uint32_t** counterArrary = cmSketch_->GetCounterArray();
    Ocall_InitWriteSealedFile(&persistenceStatus, SEALED_SKETCH);
    if (persistenceStatus == false) {
        Ocall_SGX_Exit_Error("EcallMeGA: cannot init the sketch sealed file.");
    }

    for (size_t i = 0; i < sketchDepth_; i++) {
        Enclave::WriteBufferToFile((uint8_t*)counterArrary[i], sketchWidth_ * sizeof(uint32_t), SEALED_SKETCH);
    }
    Ocall_CloseWriteSealedFile(SEALED_SKETCH);

    // step-2: persist the min-heap 
    offset = 0;
    Ocall_InitWriteSealedFile(&persistenceStatus, SEALED_FREQ_INDEX);
    if (persistenceStatus == false) {
        Ocall_SGX_Exit_Error("EcallMeGA: cannot init the heap sealed file.");
    }

    auto heapPtr = &insideDedupIndex_->_heap;
    itemNum = heapPtr->size();
    requiredBufferSize = sizeof(size_t) + itemNum * (CHUNK_HASH_SIZE + sizeof(HeapItem_t));
    tmpBuffer = (uint8_t*) malloc(sizeof(uint8_t) * requiredBufferSize);
    memcpy(tmpBuffer + offset, &itemNum, sizeof(size_t));
    offset += sizeof(size_t);
    for (size_t i = 0; i < itemNum; i++) {
        memcpy(tmpBuffer + offset, &(*heapPtr)[i]->first[0], CHUNK_HASH_SIZE);
        offset += CHUNK_HASH_SIZE;
        memcpy(tmpBuffer + offset, &(*heapPtr)[i]->second, sizeof(HeapItem_t));
        offset += sizeof(HeapItem_t);
    }
    Enclave::WriteBufferToFile(tmpBuffer, requiredBufferSize, SEALED_FREQ_INDEX);
    Ocall_CloseWriteSealedFile(SEALED_FREQ_INDEX);

    free(tmpBuffer);
    return true;
}

/**
 * @brief read the hook index from sealed data
 * 
 * @return true success
 * @return false fail
 */
bool EcallMeGA::LoadDedupIndex() {
    size_t itemNum;
    string tmpChunkFp;
    tmpChunkFp.resize(CHUNK_HASH_SIZE, 0);
    size_t sealedDataSize;
    size_t offset = 0;

    // step-1: load the sketch state 
    uint32_t** counterArray = cmSketch_->GetCounterArray();
    Ocall_InitReadSealedFile(&sealedDataSize, SEALED_SKETCH);
    if (sealedDataSize == 0) {
        return false;
    }   

    for (size_t i = 0; i < sketchDepth_; i++) {
        Enclave::ReadFileToBuffer((uint8_t*)counterArray[i], sizeof(uint32_t) * sketchWidth_, SEALED_SKETCH);
    }
    Ocall_CloseReadSealedFile(SEALED_SKETCH);

    // step-2: load the min-heap 
    auto heapPtr = &insideDedupIndex_->_heap;
    auto indexPtr = &insideDedupIndex_->_index;
    Ocall_InitReadSealedFile(&sealedDataSize, SEALED_FREQ_INDEX);
    if (sealedDataSize == 0) {
        return false;
    }

    uint8_t* tmpIndexBuffer = (uint8_t*) malloc(sealedDataSize * sizeof(uint8_t));
    Enclave::ReadFileToBuffer(tmpIndexBuffer, sealedDataSize, SEALED_FREQ_INDEX);
    memcpy(&itemNum, tmpIndexBuffer + offset, sizeof(size_t));
    offset += sizeof(size_t);
    HeapItem_t tmpItem;
    string tmpFp;
    tmpFp.resize(CHUNK_HASH_SIZE, 0);
    for (size_t i = 0; i < itemNum; i++) {
        memcpy(&tmpChunkFp[0], tmpIndexBuffer + offset, CHUNK_HASH_SIZE);
        offset += CHUNK_HASH_SIZE;
        memcpy(&tmpItem, tmpIndexBuffer + offset, sizeof(HeapItem_t));
        offset += sizeof(HeapItem_t);
        auto tmpIt = indexPtr->insert({tmpChunkFp, tmpItem}).first;
        heapPtr->push_back(tmpIt);
    }
    Ocall_CloseReadSealedFile(SEALED_FREQ_INDEX);

    free(tmpIndexBuffer);
    return true; 
}

/**
 * @brief check whether add this chunk to the heap
 * 
 * @param chunkFreq the chunk freq
 */
bool EcallMeGA::CheckIfAddToHeap(uint32_t chunkFreq) {
    if (insideDedupIndex_->Size() < topThreshold_) {
        return true;
    }
    // step: get the min-freq of current heap
    uint32_t currentMin = insideDedupIndex_->TopEntry();
    if (chunkFreq >= currentMin) {
        // the input chunk freq is larger than existing one, can add to the heap
        return true;
    }
    // the input chunk freq is lower than existsing one, cannot add to the heap 
    return false;
}

/**
 * @brief Add the information of this chunk to the heap
 * 
 * @param chunkFreq the chunk freq
 * @param chunkAddr the chunk address
 * @param chunkFp the chunk fp
 */
void EcallMeGA::AddChunkToHeap(uint32_t chunkFreq, RecipeEntry_t* chunkAddr, 
    const string& chunkFp) {
    HeapItem_t tmpHeapEntry;
    // pop the minimum item
    if (insideDedupIndex_->Size() == topThreshold_) {
        insideDedupIndex_->Pop();
    }
    // insert the new one
    memcpy(&tmpHeapEntry.address, chunkAddr, sizeof(RecipeEntry_t));
    tmpHeapEntry.chunkFreq = chunkFreq;
    insideDedupIndex_->Add(chunkFp, tmpHeapEntry);
    return ;
}


/**
 * @brief process one batch
 * 
 * @param recvChunkBuf the recv chunk buffer
 * @param upOutSGX the pointer to the enclave-related var 
 */
void EcallMeGA::ProcessOneBatch(SendMsgBuffer_t* recvChunkBuf, 
    UpOutSGX_t* upOutSGX) {
    // the in-enclave info
    EnclaveClient* sgxClient = (EnclaveClient*)upOutSGX->sgxClient;
    EVP_CIPHER_CTX* cipherCtx = sgxClient->_cipherCtx;
    EVP_MD_CTX* mdCtx = sgxClient->_mdCtx;
    uint8_t* recvBuffer = sgxClient->_recvBuffer;
    uint8_t* sessionKey = sgxClient->_sessionKey;
    Recipe_t* inRecipe = &sgxClient->_inRecipe;
    InQueryEntry_t* inQueryBase = sgxClient->_inQueryBase;
    OutQueryEntry_t* outQueryBase = upOutSGX->outQuery->outQueryBase;

    // for edelta;
    BaseLink_ = sgxClient->BaseLink_;
    cutEdelta_ = sgxClient->cutEdelta_;
    psHTable_ = &(sgxClient->psHTable_);
    psHTable2_ = sgxClient->psHTable2_;
    encBaseBuffer_ = sgxClient->encBaseBuffer_;
    decBaseBuffer_ = sgxClient->decBaseBuffer_;
    plainBaseBuffer_ = sgxClient->plainBaseBuffer_;
    ivBuffer_ = sgxClient->ivBuffer_;
    deltaBuffer_ = sgxClient->deltaBuffer_;

    // tmp var
    OutQueryEntry_t* outQueryEntry = outQueryBase;
    uint32_t outQueryNum = 0;
    string tmpHashStr;
    tmpHashStr.resize(CHUNK_HASH_SIZE, 0);

    // for MeGA checker
    unordered_map<string,int> MeGA_map;
    int batch_out_times = 0;

    // decrypt the received data with the session key
    cryptoObj_->SessionKeyDec(cipherCtx, recvChunkBuf->dataBuffer,
        recvChunkBuf->header->dataSize, sessionKey, recvBuffer);
    
    // get the chunk num
    uint32_t chunkNum = recvChunkBuf->header->currentItemNum;

    // compute the hash of each chunk
    InQueryEntry_t* inQueryEntry = inQueryBase;
    size_t currentOffset = 0;
    for (size_t i = 0; i < chunkNum; i++) {
        // compute the hash over the plaintext chunk
        memcpy(&inQueryEntry->chunkSize, recvBuffer + currentOffset,
            sizeof(uint32_t));
        currentOffset += sizeof(uint32_t);

        cryptoObj_->GenerateHash(mdCtx, recvBuffer + currentOffset,
            inQueryEntry->chunkSize, inQueryEntry->chunkHash);
        currentOffset += inQueryEntry->chunkSize;
        inQueryEntry++;
    }

{
#if (MULTI_CLIENT == 1)
    Enclave::sketchLck_.lock();
#endif
    // update the sketch and freq
    inQueryEntry = inQueryBase;
    for (size_t i = 0; i < chunkNum; i++) {
        cmSketch_->Update(inQueryEntry->chunkHash, CHUNK_HASH_SIZE, 1);
        inQueryEntry->chunkFreq = cmSketch_->Estimate(inQueryEntry->chunkHash,
            CHUNK_HASH_SIZE);
        inQueryEntry++;
    }
#if (MULTI_CLIENT == 1)
    Enclave::sketchLck_.unlock();
#endif
}

{
#if (MULTI_CLIENT == 1)
    Enclave::topKIndexLck_.lock();
#endif
    // check the top-k index
    uint32_t minFreq;
    if (insideDedupIndex_->Size() == topThreshold_) {
        minFreq = insideDedupIndex_->TopEntry();
    } else {
        minFreq = 0;
    }
    inQueryEntry = inQueryBase;
    
    for (size_t i = 0; i < chunkNum; i++) {
        tmpHashStr.assign((char*)inQueryEntry->chunkHash, CHUNK_HASH_SIZE);
        auto findRes = sgxClient->_localIndex.find(tmpHashStr);
        if(findRes != sgxClient->_localIndex.end()) {
            // it exist in this local batch index
            uint32_t offset = findRes->second;
            InQueryEntry_t* findEntry = inQueryBase + offset; 
            switch (findEntry->dedupFlag) {
                case UNIQUE: {
                    // this chunk is unique for the top-k index, but duplicate for the local index
                    inQueryEntry->dedupFlag = TMP_UNIQUE;
                    inQueryEntry->chunkAddr.offset = offset;
                    break;
                }
                case DUPLICATE: {
                    // this chunk is duplicate for the heap and the local index
                    inQueryEntry->dedupFlag = TMP_DUPLICATE;
                    inQueryEntry->chunkAddr.offset = offset;
                    break;
                }
                default: {
                    Ocall_SGX_Exit_Error("EcallMeGA: wrong in-enclave dedup flag");
                }
            }

            // update the freq
            findEntry->chunkFreq = inQueryEntry->chunkFreq;
        } else {
            // it does not exists in the batch index, compare the freq 
            if (inQueryEntry->chunkFreq < minFreq) {
                // its frequency is smaller than the minimum value in the heap, must not exist in the heap
                // encrypt its fingerprint, write to the outside buffer
                cryptoObj_->IndexAESCMCEnc(cipherCtx, inQueryEntry->chunkHash,
                    CHUNK_HASH_SIZE, Enclave::indexQueryKey_, outQueryEntry->chunkHash);

                uint8_t tmphash[CHUNK_HASH_SIZE];
                memcpy(&tmphash[0],&outQueryEntry->chunkHash[0],CHUNK_HASH_SIZE);
                //Ocall_PrintfBinary(&tmphash[0],CHUNK_HASH_SIZE);

                // update the in-enclave query buffer
                inQueryEntry->dedupFlag = UNIQUE;
                inQueryEntry->chunkAddr.offset = outQueryNum;

                // update the out-enclave query buffer
                outQueryEntry++;
                outQueryNum++;
            } else {
                // its frequency is higher than the minimum value in the heap, check the heap
                bool topKRes = insideDedupIndex_->Contains(tmpHashStr);
                if (topKRes) {
                    // it exists in the heap, directly read
                    inQueryEntry->dedupFlag = DUPLICATE;
                    memcpy(&inQueryEntry->chunkAddr, insideDedupIndex_->GetPriority(tmpHashStr),
                        sizeof(RecipeEntry_t));
                } else {
                    // it does not exist in the heap
                    cryptoObj_->IndexAESCMCEnc(cipherCtx, inQueryEntry->chunkHash, CHUNK_HASH_SIZE,
                        Enclave::indexQueryKey_, outQueryEntry->chunkHash);

                    uint8_t tmphash[CHUNK_HASH_SIZE];
                    memcpy(&tmphash[0],&outQueryEntry->chunkHash[0],CHUNK_HASH_SIZE);
                    //Ocall_PrintfBinary(&tmphash[0],CHUNK_HASH_SIZE);
                    
                    // update the dedup list
                    inQueryEntry->dedupFlag = UNIQUE;
                    inQueryEntry->chunkAddr.offset = outQueryNum;

                    // update the out-enclave query buffer
                    outQueryEntry++;
                    outQueryNum++;
                }
            }

            sgxClient->_localIndex[tmpHashStr] = i;
        }
        inQueryEntry++;
    }
#if (MULTI_CLIENT == 1)
    Enclave::topKIndexLck_.unlock();
#endif
}

    //Enclave::Logging("DE BUG","Indedup Down");
    // check the out-enclave index
    if (outQueryNum != 0) {
        upOutSGX->outQuery->queryNum = outQueryNum;
        Ocall_QueryOutIndex(upOutSGX->outClient);
        _Inline_Ocall++;
    }
    //Enclave::Logging("DE BUG","Outdedup Down");
    //为每一个Unique chunk计算superfeature
    inQueryEntry = inQueryBase;
    outQueryEntry = upOutSGX->outQuery->outQueryBase;
    currentOffset = 0;

    //Enclave::Logging("DE BUG","Process In");
    for(size_t i = 0; i < chunkNum; i++){
        currentOffset += sizeof(uint32_t);
        if(inQueryEntry->dedupFlag == UNIQUE){
            if(outQueryEntry->dedupFlag == UNIQUE){

#if(SF_SINGLE_THREAD == 1)
                getSF2(recvBuffer + currentOffset,mdCtx,(uint8_t *)&inQueryEntry->superfeature,this->cryptoObj_,inQueryEntry->chunkSize);
                outQueryEntry->deltaFlag = NO_DELTA;
                outQueryEntry->offlineFlag = 0;
                memcpy(&outQueryEntry->superfeature, &inQueryEntry->superfeature, 3 * CHUNK_HASH_SIZE);
#else
                Param test_param;
                test_param.ptr = recvBuffer + currentOffset;
                test_param.mdCtx = mdCtx;
                test_param.SF = (uint8_t *)&inQueryEntry->superfeature;
                test_param.cryptoObj_ = this->cryptoObj_;
                test_param.chunksize = inQueryEntry->chunkSize;
                param_list.push_back(test_param);

#endif
            }
            outQueryEntry++;
        }
        currentOffset += inQueryEntry->chunkSize;
        inQueryEntry++;
    }

#if(SF_SINGLE_THREAD == 0)
    const size_t thread_num = 3;
    int block_len;
    block_len = param_list.size() / thread_num;
    pthread_mutex_init(&mutex2,NULL);
    std::vector<GetSFTask> ts(thread_num,GetSFTask());
    for(int i = 0;i< thread_num; i++){
        auto &t = ts[i];
        t.begin = block_len * i;
        t.end = block_len * (i+1);
        t.thread_id = i;
        if(i == thread_num - 1&&(param_list.size() % thread_num != 0)){
            t.end = param_list.size();
        }
        pthread_create(&test_pthread[i],NULL,GetSF_thread_func_f,&t);
    }

    for(int i = 0;i< thread_num;i++){
        pthread_join(test_pthread[i],NULL);
    }

    pthread_mutex_destroy(&mutex2);
    
    param_list.clear();

#endif

#if(SF_SINGLE_THREAD == 0)

    OutEntrySFGet(inQueryBase,outQueryBase,chunkNum);

#endif
    //Enclave::Logging("DE BUG","SF Down");
    Ocall_QueryOutBasechunk(upOutSGX->outClient);
    _Inline_Ocall++;

    MeGAChecker(inQueryBase,outQueryBase,upOutSGX,chunkNum,MeGA_map);
    //Enclave::Logging("DE BUG","Checker Down");
    //process the unique chunks and update the metadata
    inQueryEntry = inQueryBase;
    outQueryEntry = upOutSGX->outQuery->outQueryBase;
    currentOffset = 0;
    string tmpChunkAddr;
    tmpChunkAddr.resize(sizeof(RecipeEntry_t), 0);
    InQueryEntry_t* tmpQueryEntry;
    uint32_t tmpChunkSize;
    for (size_t i = 0; i < chunkNum; i++) {
        tmpChunkSize = inQueryEntry->chunkSize;
        currentOffset += sizeof(uint32_t);
        switch (inQueryEntry->dedupFlag) {
            case DUPLICATE: {
                // it is duplicate for the min-heap
                tmpChunkAddr.assign((char*)&inQueryEntry->chunkAddr,
                    sizeof(RecipeEntry_t));
                
                // update the statistic
                insideDedupChunkNum_++;
                insideDedupDataSize_ += tmpChunkSize;
                break;    
            }
            case TMP_DUPLICATE: {
                // it is also duplicate, for the local index
                tmpQueryEntry = inQueryBase + inQueryEntry->chunkAddr.offset;
                tmpChunkAddr.assign((char*)&tmpQueryEntry->chunkAddr,
                    sizeof(RecipeEntry_t));
                
                // update the statistic
                insideDedupChunkNum_++;
                insideDedupDataSize_ += tmpChunkSize;
                break;
            }
            case UNIQUE: {
                // it is unique for the top-k index
                switch (outQueryEntry->dedupFlag) {
                    case DUPLICATE: {
                        // it is duplicate for the out-enclave index
                        cryptoObj_->AESCBCDec(cipherCtx, (uint8_t*)&outQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t), Enclave::indexQueryKey_,
                            (uint8_t*)&inQueryEntry->chunkAddr);
                        tmpChunkAddr.assign((char*)&inQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t));
                        break;
                    }
                    case UNIQUE: {
                        // it also unique for the out-enclave index
                        inQueryEntry->deltaFlag = NO_DELTA;
                        inQueryEntry->chunkAddr.deltaFlag = NO_DELTA;

                        MeGAdeltaTure(inQueryEntry,outQueryEntry,upOutSGX,batch_out_times,MeGA_map);

                        uint8_t *deltachunk;
                        size_t deltachunk_size;

                        //outQueryEntry->deltaFlag = NO_DELTA;
                        
                        if (outQueryEntry->deltaFlag == DELTA)
                        {   //Enclave::Logging("DE BUG","Delta IN");
                            deltachunk = ProcessDeltachunk(inQueryEntry,outQueryEntry,upOutSGX,currentOffset,recvBuffer,&deltachunk_size);
                            //Enclave::Logging("DE BUG","Delta OUT");
                        }
                        
                       //inQueryEntry->chunkAddr.deltaFlag = NO_DELTA;

                        if(inQueryEntry->chunkAddr.deltaFlag == DELTA){
                            outQueryEntry->deltaFlag = DELTA;
                            this->ProcessSheUniqueChunk(&inQueryEntry->chunkAddr,deltachunk,deltachunk_size, upOutSGX, (uint8_t *)&inQueryEntry->superfeature, (uint8_t *)&outQueryEntry->chunkHash);
                            // free(deltachunk);
                            // string baseChunkHash;
                            // string deltaChunkHash;
                            // baseChunkHash.assign((char*)&outQueryEntry->chunkAddr.basechunkHash[0], CHUNK_HASH_SIZE);
                            // deltaChunkHash.assign((char*)&outQueryEntry->chunkHash[0], CHUNK_HASH_SIZE);
                            // memset(upOutSGX->process_buffer, 0, 1000 * CHUNK_HASH_SIZE);
                            // memcpy(upOutSGX->process_buffer, (uint8_t*)&baseChunkHash[0], CHUNK_HASH_SIZE);
                            // memcpy(upOutSGX->process_buffer + CHUNK_HASH_SIZE, (uint8_t*)&deltaChunkHash[0], CHUNK_HASH_SIZE);
                            // Ocall_UpdateDeltaIndex(upOutSGX->outClient);
                            _deltaChunkNum++;
                            _deltaDataSize += inQueryEntry->chunkAddr.length;
                            _DeltaSaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                            _Online_DeltaSaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                            _Inline_Ocall++;


                        }else if (inQueryEntry->chunkAddr.deltaFlag == NO_DELTA){
                            outQueryEntry->deltaFlag = NO_DELTA;
                            this->ProcessSheUniqueChunk(&inQueryEntry->chunkAddr,recvBuffer + currentOffset, tmpChunkSize, upOutSGX, (uint8_t *)&inQueryEntry->superfeature, (uint8_t *)&outQueryEntry->chunkHash);

                            _baseChunkNum++;
                            _baseDataSize += inQueryEntry->chunkAddr.length;
                            _lz4SaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                        }
                        //Enclave::Logging("DE BUG","Storage Down");
                        tmpChunkAddr.assign((char*)&inQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t));

                        // encrypt the chunk address, write to the out-enclave buffer
                        cryptoObj_->AESCBCEnc(cipherCtx, (uint8_t*)&inQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t), Enclave::indexQueryKey_,
                            (uint8_t*)&outQueryEntry->chunkAddr);

                        // update the statistic
                        _uniqueChunkNum++;
                        _uniqueDataSize += tmpChunkSize;
                        break;
                    }
                    default: {
                        Ocall_SGX_Exit_Error("EcallMeGA: wrong out-enclave dedup flag");
                    }
                }
                upOutSGX->outQuery->currNum++;
                outQueryEntry++;
                break;
            }
            case TMP_UNIQUE: {
                // it is unique for the top-k index, but duplicate in local index
                tmpQueryEntry = inQueryBase + inQueryEntry->chunkAddr.offset;
                tmpChunkAddr.assign((char*)&tmpQueryEntry->chunkAddr,
                    sizeof(RecipeEntry_t));
                
                // update statistic
                insideDedupChunkNum_++;
                insideDedupDataSize_ += tmpChunkSize;
                break;
            }
            default: {
                Ocall_SGX_Exit_Error("EcallMeGA: wrong chunk status flag");
            }
        }
        this->UpdateFileRecipe(tmpChunkAddr, inRecipe,upOutSGX,&inQueryEntry->chunkHash[0]);
        

        //Ocall_PrintfBinary(&inQueryEntry->chunkHash[0],CHUNK_HASH_SIZE);

        inQueryEntry++;
        currentOffset += tmpChunkSize;

        // update the statistic
        _logicalDataSize += tmpChunkSize;
        _logicalChunkNum++;
    }

    // inQueryEntry = inQueryBase;
    // outQueryEntry = upOutSGX->outQuery->outQueryBase;
    // memset(upOutSGX->process_buffer, 0, chunkNum * CHUNK_HASH_SIZE);
    // uint32_t bufferOffset = 0;
    // string baseChunkHash;
    // string deltaChunkHash;
    // for (size_t i = 0; i < chunkNum; i++)
    // {
    //     if(inQueryEntry->chunkAddr.deltaFlag == DELTA){
    //         baseChunkHash.assign((char*)&outQueryEntry->chunkAddr.basechunkHash[0], CHUNK_HASH_SIZE);
    //         deltaChunkHash.assign((char*)&outQueryEntry->chunkHash[0], CHUNK_HASH_SIZE);

    //         memcpy(upOutSGX->process_buffer + bufferOffset, (uint8_t*)&baseChunkHash[0], CHUNK_HASH_SIZE);
    //         bufferOffset += CHUNK_HASH_SIZE;
    //         memcpy(upOutSGX->process_buffer + bufferOffset, (uint8_t*)&deltaChunkHash[0], CHUNK_HASH_SIZE);
    //         bufferOffset += CHUNK_HASH_SIZE;
    //     }
    // }
    // size_t chunkNumTmp = chunkNum;
    // Ocall_UpdateDeltaIndex(upOutSGX->outClient, chunkNumTmp);
    // _Inline_Ocall++;
    // _Inline_DeltaOcall++;

    //Enclave::Logging("DE BUG","Process Down");



{
#if (MULTI_CLIENT == 1)
    Enclave::topKIndexLck_.lock();
#endif
    // update the min-heap
    inQueryEntry = inQueryBase;
    for (size_t i = 0; i < chunkNum; i++) {
        if (inQueryEntry->dedupFlag == UNIQUE || 
            inQueryEntry->dedupFlag == DUPLICATE) {
            uint32_t chunkFreq = inQueryEntry->chunkFreq;
            if (this->CheckIfAddToHeap(chunkFreq)) {
                // add this chunk to the top-k index
                tmpHashStr.assign((char*)inQueryEntry->chunkHash, CHUNK_HASH_SIZE);
                if (insideDedupIndex_->Contains(tmpHashStr)) {
                    // it exists in the min-heap
                    this->UpdateInsideIndexFreq(tmpHashStr, chunkFreq);
                } else {
                    // it does not exist in the min-heap
                    this->AddChunkToHeap(chunkFreq, &inQueryEntry->chunkAddr,
                        tmpHashStr);
                }
            }
        }
        inQueryEntry++;
    }
#if (MULTI_CLIENT == 1)
    Enclave::topKIndexLck_.unlock();
#endif
}
    // update the out-enclave index
    upOutSGX->outQuery->queryNum = outQueryNum;
    upOutSGX->outQuery->currNum = 0;
    sgxClient->_localIndex.clear();
    //Enclave::Logging("DE BUG","All down\n");
    return ;
}




uint8_t* EcallMeGA::ProcessDeltachunk(InQueryEntry_t *inQueryEntry, OutQueryEntry_t *outQueryEntry, UpOutSGX_t *upOutSGX,size_t currentOffset,uint8_t* recvBuffer, size_t *deltachunk_size)
{
    uint8_t *reccchunk;
    uint8_t* deltachunk;
    size_t reccchunk_size = 1;
    int refchunksize;
    // uint8_t temp_decompressChunk[MAX_CHUNK_SIZE];
    EnclaveClient *sgxClient = (EnclaveClient *)upOutSGX->sgxClient;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t* tmpbuffer;
    string tmpContainerIDStr_1;
    tmpContainerIDStr_1.assign((char *)inQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);

    bool result = InContainercache_->ExistsInCache(tmpContainerIDStr_1);
    if(result == 0){
        Enclave::Logging(myName_.c_str(), "IScontainer:%d\n",result);
    }
    tmpbuffer = outQueryEntry->containerbuffer; 
    // Enclave::Logging(myName_.c_str(), "before enc base buffer\n");
    memcpy(encBaseBuffer_, 
        tmpbuffer + inQueryEntry->basechunkAddr.offset + sizeof(RecipeEntry_t) + 4 * CHUNK_HASH_SIZE, 
        inQueryEntry->basechunkAddr.length);
    memcpy(ivBuffer_, 
        tmpbuffer + inQueryEntry->basechunkAddr.offset + inQueryEntry->basechunkAddr.length + sizeof(RecipeEntry_t) + 4 * CHUNK_HASH_SIZE, 
        CRYPTO_BLOCK_SIZE);

    // Decrypt basechunk with iv-key
    cryptoObj_->DecryptionWithKeyIV(cipherCtx, encBaseBuffer_, 
        outQueryEntry->basechunkAddr.length, Enclave::enclaveKey_, decBaseBuffer_, ivBuffer_);
 
    // Decode basechunk by lz4
    refchunksize = LZ4_decompress_safe((char *)decBaseBuffer_, 
        (char *)plainBaseBuffer_, outQueryEntry->basechunkAddr.length, MAX_CHUNK_SIZE);

    // do delta compression
    if (refchunksize > 0)
    {
        deltachunk = ed3_encode(recvBuffer + currentOffset, inQueryEntry->chunkSize, 
            plainBaseBuffer_, refchunksize, deltachunk_size, deltaBuffer_);
        // reccchunk = ed3_decode(deltachunk, *deltachunk_size, basechunkbuffer, refchunksize, &reccchunk_size);
        if (*deltachunk_size >= inQueryEntry->chunkSize)
        {
            outQueryEntry->deltaFlag = NO_DELTA;
            // free(deltachunk);
        }
        // free(reccchunk);
    }
    else
    {
        deltachunk = ed3_encode(recvBuffer + currentOffset, inQueryEntry->chunkSize, 
            decBaseBuffer_, outQueryEntry->basechunkAddr.length, deltachunk_size, deltaBuffer_);
        // reccchunk = ed3_decode(deltachunk, *deltachunk_size, basechunkbuffer, refchunksize, &reccchunk_size);
        if (*deltachunk_size >= inQueryEntry->chunkSize)
        {
            outQueryEntry->deltaFlag = NO_DELTA;
            // free(deltachunk);
        }
        // free(reccchunk);
    }
    // deltachunk = (uint8_t*)malloc(MAX_CHUNK_SIZE);
    // *deltachunk_size = MAX_CHUNK_SIZE;

    inQueryEntry->chunkAddr.deltaFlag = outQueryEntry->deltaFlag;
    return deltachunk;
}

/**
 * @brief Use Xdelta3 method for delta compression
 *
 * @param in the pointer of the unique chunk content
 * @param in_size the size of unique chunk
 * @param ref the pointer of the basechunk content
 * @param ref_size the size of basechunk
 * @param res_size the size of delta chunk
 *
 * @return the pointer of the delta chunk
 */
uint8_t *EcallMeGA::xd3_encode(const uint8_t *in, size_t in_size, const uint8_t *ref, size_t ref_size, size_t *res_size) // 更改函数
{   
    const auto max_buffer_size = (in_size + ref_size) * 2;
    uint8_t *buffer;
    buffer = (uint8_t *)malloc(max_buffer_size);
    size_t sz;
    xd3_encode_memory(in, in_size, ref, ref_size, buffer, &sz, max_buffer_size, 0);
    uint8_t *res;
    res = (uint8_t *)malloc(sz);
    *res_size = sz;
    memcpy(res, buffer, sz);
    free(buffer);
    return res;
}


/**
 * @brief Use Xdelta3 method for delta decompression
 *
 * @param in the pointer of the delta chunk content
 * @param in_size the size of delta chunk
 * @param ref the pointer of the basechunk content
 * @param ref_size the size of basechunk
 * @param res_size the size of restore chunk
 *
 * @return the pointer of the restore chunk
 */
uint8_t *EcallMeGA::xd3_decode(const uint8_t *in, size_t in_size, const uint8_t *ref, size_t ref_size, size_t *res_size) // 更改函数
{
    const auto max_buffer_size = (in_size + ref_size) * 2;
    uint8_t *buffer;
    buffer = (uint8_t *)malloc(max_buffer_size);
    size_t sz;
    xd3_decode_memory(in, in_size, ref, ref_size, buffer, &sz, max_buffer_size, 0);
    uint8_t *res;
    res = (uint8_t *)malloc(sz);
    *res_size = sz;
    memcpy(res, buffer, sz);
    free(buffer);
    return res;
}

void EcallMeGA::ProcessOffline(SendMsgBuffer_t *recvChunkBuf, UpOutSGX_t *upOutSGX)
{
    offlinebackOBj_->_offlineCompress_size = _onlineCompress_size;
    offlinebackOBj_->_offlineCurrBackup_size = _onlineBackupSize;
    offlinebackOBj_->_offlineCompress_size = _onlineCompress_size;
    offlinebackOBj_->_offlineCurrBackup_size = _onlineBackupSize;
    offlinebackOBj_->_baseChunkNum = _baseChunkNum;
    offlinebackOBj_->_baseDataSize = _baseDataSize;
    offlinebackOBj_->_deltaChunkNum = _deltaChunkNum;
    offlinebackOBj_->_deltaDataSize = _deltaDataSize;
    offlinebackOBj_->_DeltaSaveSize = _DeltaSaveSize;
    offlinebackOBj_->_lz4SaveSize = _lz4SaveSize;
    return ;
}

void EcallMeGA::MeGAdeltaTure(InQueryEntry_t *_inQueryEntry, OutQueryEntry_t *_outQueryEntry, UpOutSGX_t *_upOutSGX,int &_batch_out_times,unordered_map<string,int> &MeGA_ContainerIDmap)
{

    EnclaveClient *sgxClient = (EnclaveClient *)_upOutSGX->sgxClient;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t* tmpcontainer =  _upOutSGX->outcallcontainer;
    int container_flag = 1;

    string tmpContainerIDStr;
    tmpContainerIDStr.assign((char *)_outQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);
    
    bool MeGA_Flag;
    auto it = MeGA_ContainerIDmap.find(tmpContainerIDStr);

    if(it!=MeGA_ContainerIDmap.end() && it->second > MeGA_THRESOLD){
        MeGA_Flag = false;
    }else{
        MeGA_Flag = true;
    }

    if(MeGA_Flag&&_outQueryEntry->deltaFlag == OUT_DELTA){
        _outQueryEntry->chunkAddr.deltaFlag = NO_DELTA;
        return;
    }

    if(_outQueryEntry->deltaFlag == OUT_DELTA){

    if(InContainercache_->ExistsInCache(tmpContainerIDStr)){
        _outQueryEntry->containerbuffer = InContainercache_->ReadFromCache(tmpContainerIDStr);
        memcpy(&_inQueryEntry->chunkAddr.basechunkHash, &_outQueryEntry->chunkAddr.basechunkHash, CHUNK_HASH_SIZE);
        _outQueryEntry->deltaFlag = DELTA;
        return; 
    }

    }

   
    if (_outQueryEntry->deltaFlag == OUT_DELTA)
    {
        string tmpContainerIDStr_1;
        tmpContainerIDStr_1.assign((char *)_outQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);
        Ocall_getRefContainer(_upOutSGX->outClient);
        _Inline_Ocall++;
  
    }

    // add the container to in-container cache
    if (_outQueryEntry->deltaFlag == DELTA)
    {
        string tmpContainerIDStr;
        tmpContainerIDStr.assign((char *)_outQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);
        if (container_flag == 1)
        {
            if (!InContainercache_->ExistsInCache(tmpContainerIDStr))
            {
                InContainercache_->InsertToCache_Offline(tmpContainerIDStr, (char *)_outQueryEntry->containerbuffer,MAX_CONTAINER_SIZE);
                basecontainer_set.insert(tmpContainerIDStr);
                _outQueryEntry->containerbuffer = InContainercache_->ReadFromCache(tmpContainerIDStr);
                memcpy(&_inQueryEntry->chunkAddr.basechunkHash, &_outQueryEntry->chunkAddr.basechunkHash, CHUNK_HASH_SIZE);
                _batch_out_times++;
            }
        }
    }

}

void EcallMeGA::MeGALoad(InQueryEntry_t *_inQueryEntry, OutQueryEntry_t *_outQueryEntry, UpOutSGX_t *_upOutSGX,unordered_map<string,int> &MeGA_ContainerIDmap){
    EnclaveClient *sgxClient = (EnclaveClient *)_upOutSGX->sgxClient;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t* tmpcontainer =  _upOutSGX->outcallcontainer;

    if (_outQueryEntry->deltaFlag == OUT_DELTA)
    {
    memcpy(&_inQueryEntry->chunkAddr.basechunkHash, &_outQueryEntry->chunkAddr.basechunkHash, CHUNK_HASH_SIZE);
    cryptoObj_->AESCBCDec(cipherCtx, (uint8_t *)&_outQueryEntry->basechunkAddr, sizeof(RecipeEntry_t), Enclave::indexQueryKey_, (uint8_t *)&_inQueryEntry->basechunkAddr);
    memcpy(&_outQueryEntry->basechunkAddr,&_inQueryEntry->basechunkAddr,sizeof(RecipeEntry_t));
    //_outQueryEntry->basechunkAddr = _inQueryEntry->basechunkAddr;

    string ContainerIDstr;
    ContainerIDstr.resize(CONTAINER_ID_LENGTH,0);
    ContainerIDstr.assign((char*)_inQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);

    auto it = MeGA_ContainerIDmap.find(ContainerIDstr);
    if(it == MeGA_ContainerIDmap.end()){
        MeGA_ContainerIDmap[ContainerIDstr] = 1;
    }else{
        MeGA_ContainerIDmap[ContainerIDstr]++;
    }

    }
    return;
}

bool EcallMeGA::MeGAChecker(InQueryEntry_t *_inQueryBase, OutQueryEntry_t *_outQueryBase, UpOutSGX_t *_upOutSGX,uint32_t _chunkNum,unordered_map<string,int> &MeGA_ContainerIDmap){
    InQueryEntry_t *inQueryEntry = _inQueryBase;
    OutQueryEntry_t *outQueryEntry = _outQueryBase;
    set<string> Batch_ContainerIDset;
    for(size_t i = 0; i < _chunkNum; i++){
        if(inQueryEntry->dedupFlag == UNIQUE){
            if(outQueryEntry->dedupFlag == UNIQUE){
                MeGALoad(inQueryEntry,outQueryEntry,_upOutSGX,MeGA_ContainerIDmap);
            }
            outQueryEntry++;
        }
        inQueryEntry++;
    }
    return true;
}


void EcallMeGA::OutEntrySFGet(InQueryEntry_t *_inQueryBase, OutQueryEntry_t *_outQueryBase,uint32_t _chunkNum){
    InQueryEntry_t *inQueryEntry = _inQueryBase;
    OutQueryEntry_t *outQueryEntry = _outQueryBase;
    for(size_t i = 0; i < _chunkNum; i++){
        if(inQueryEntry->dedupFlag == UNIQUE){
            if(outQueryEntry->dedupFlag == UNIQUE){
                outQueryEntry->deltaFlag = NO_DELTA;
                outQueryEntry->offlineFlag = 0;
                memcpy(&outQueryEntry->superfeature, &inQueryEntry->superfeature, 3 * CHUNK_HASH_SIZE);
            }
            outQueryEntry++;
        }
        inQueryEntry++;
    }
    return;

}

uint8_t *EcallMeGA::ed3_encode(uint8_t *in, size_t in_size, uint8_t *ref, 
    size_t ref_size, size_t *res_size, uint8_t *tmpbuffer) // 更改函数
{
    uint32_t sz;
    // Enclave::Logging(myName_.c_str(), "before edelta encode\n");
    int resInt = this->EDeltaEncode(in, in_size, ref, ref_size, tmpbuffer, &sz);
    // Enclave::Logging(myName_.c_str(), "before edelta encode\n");
    uint8_t *res;
    *res_size = sz;
    return tmpbuffer;
}

uint8_t *EcallMeGA::ed3_decode(uint8_t *in, size_t in_size, uint8_t *ref, size_t ref_size, size_t *res_size) // 更改函数
{
    // Enclave::Logging(myName_.c_str(), "max_buffer_size: %lu", max_buffer_size);
    uint8_t *buffer;
    buffer = (uint8_t *)malloc(MAX_CHUNK_SIZE * 2);
    uint32_t res32;
    size_t sz;
    // xd3_decode_memory(in, in_size, ref, ref_size, buffer, &sz, max_buffer_size, 0);
    int resInt = this->EDeltaDecode(in, in_size, ref, ref_size, buffer, &res32);
    res32 = 1024;
    sz = res32;
    uint8_t *res;
    res = (uint8_t *)malloc(sz);
    *res_size = sz;
    memcpy(res, buffer, sz);
    free(buffer);
    return res;
}


/* flag=0 for 'D', 1 for 'S' */
void EcallMeGA::set_flag(void *record, uint32_t flag) {
  uint32_t *flag_length = (uint32_t *)record;
  if (flag == 0) {
    (*flag_length) &= ~(uint32_t)0 >> 1;
  } else {
    (*flag_length) |= (uint32_t)1 << 31;
  }
}

/* return 0 if flag=0, >0(not 1) if flag=1 */
u_int32_t EcallMeGA::get_flag(void *record) {
  u_int32_t *flag_length = (u_int32_t *)record;
  return (*flag_length) & (u_int32_t)1 << 31;
}

void EcallMeGA::set_length(void *record, uint32_t length) {
  uint32_t *flag_length = (uint32_t *)record;
  uint32_t musk = (*flag_length) & (uint32_t)1 << 31;
  *flag_length = length | musk;
}

uint32_t EcallMeGA::get_length(void *record) {
  uint32_t *flag_length = (uint32_t *)record;
  return (*flag_length) & ~(uint32_t)0 >> 1;
}

int EcallMeGA::Chunking_v3(unsigned char *data, int len, int num_of_chunks,
                DeltaRecord *subChunkLink) 
{
  int i = 0, *cut;
  /* cut is the chunking points in the stream */
  cut = cutEdelta_;
  int numBytes =
      rolling_gear_v3(data, len, num_of_chunks, cut); //分割给定快的总字节数

  while (i < num_of_chunks) {
    int chunkLen = cut[i + 1] - cut[i];
    subChunkLink[i].nLength = chunkLen;
    subChunkLink[i].nOffset = cut[i]; /**/
    subChunkLink[i].DupFlag = 0;
    subChunkLink[i].nHash = weakHash(data + cut[i], chunkLen);
    //	SpookyHash::Hash64(data+ cut[i], chunkLen, 0x1af1);
    i++;
  }
  return numBytes;
}

// int EcallMeGA:EDeltaEncode(uint8_t *newBuf, uint32_t newSize, uint8_t *baseBuf,
//                  uint32_t baseSize, uint8_t *deltaBuf, uint32_t *deltaSize) {
//   /* detect the head and tail of one chunk */
//   uint32_t beg = 0; 
//   uint32_t end = 0;
//   uint32_t begSize = 0;
//   uint32_t endSize = 0;
//   float matchsum = 0.;
//   float match = 0.;
// //   Enclave::Logging(myName_.c_str(), "1\n");
//   while (begSize + 7 < baseSize && begSize + 7 < newSize) {
//     if (*(uint64_t *)(baseBuf + begSize) == *(uint64_t *)(newBuf + begSize)) {
//       begSize += 8;
//     } else
//       break;
//   }
//   while (begSize < baseSize && begSize < newSize) {
//     if (baseBuf[begSize] == newBuf[begSize]) {
//       begSize++;
//     } else
//       break;
//   }

//   if (begSize > 16)
//     beg = 1;
//   else
//     begSize = 0;

//   while (endSize + 7 < baseSize && endSize + 7 < newSize) {
//     if (*(uint64_t *)(baseBuf + baseSize - endSize - 8) ==
//         *(uint64_t *)(newBuf + newSize - endSize - 8)) {
//       endSize += 8;
//     } else
//       break;
//   }
//   while (endSize < baseSize && endSize < newSize) {
//     if (baseBuf[baseSize - endSize - 1] == newBuf[newSize - endSize - 1]) {
//       endSize++;
//     } else
//       break;
//   }

//   if (begSize + endSize > newSize)
//     endSize = newSize - begSize;

//   if (endSize > 16)
//     end = 1;
//   else
//     endSize = 0;
// //   /* end of detect */
// //   Enclave::Logging(myName_.c_str(), "2\n");

//   if (begSize + endSize >= baseSize) {
//     DeltaUnit1 record1;
//     DeltaUnit2 record2;
//     uint32_t deltaLen = 0;
//     if (beg) {
//       set_flag(&record1, 0);
//       record1.nOffset = 0;
//       set_length(&record1, begSize);
//       memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
//       deltaLen += sizeof(DeltaUnit1);
//     }
//     if (newSize - begSize - endSize > 0) {
//       set_flag(&record2, 1);
//       set_length(&record2, newSize - begSize - endSize);
//       memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
//       deltaLen += sizeof(DeltaUnit2);
//       memcpy(deltaBuf + deltaLen, newBuf + begSize, get_length(&record2));
//       deltaLen += get_length(&record2);
//     }
//     if (end) {
//       set_flag(&record1, 0);
//       record1.nOffset = baseSize - endSize;
//       set_length(&record1, endSize);
//       memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
//       deltaLen += sizeof(DeltaUnit1);
//     }

//     *deltaSize = deltaLen;
//     // Enclave::Logging(myName_.c_str(), "2.9\n");
//     return deltaLen;
//   }
// //   Enclave::Logging(myName_.c_str(), "3\n");


//   uint32_t deltaLen = 0;
//   uint32_t cursor_base = begSize;
//   uint32_t cursor_input = begSize;
//   uint32_t cursor_input1 = 0;
//   uint32_t cursor_input2 = 0;
//   uint32_t input_last_chunk_beg = begSize;
//   uint32_t inputPos = begSize;
//   uint32_t length;
//   uint64_t hash;
//   DeltaRecord *psDupSubCnk = NULL;
//   DeltaUnit1 record1{0, 0};
//   DeltaUnit2 record2{0};
//   set_flag(&record1, 0);
//   set_flag(&record2, 1);
//   int flag = 0; /* to represent the last record in the deltaBuf,
//        1 for DeltaUnit1, 2 for DeltaUnit2 */

//   auto res1  = psHTable_->find(0);
//   auto res2  = psHTable_->find(0);
//   auto res3  = psHTable_->find(0);

//   int numBase = 0;  /* the total number of chunks that the base has chunked */
//   int numBytes = 0; /* the number of bytes that the base chunks once */
//   DeltaRecord *BaseLink = BaseLink_;
// //   Enclave::Logging("edelta", "malloc size: %u\n", sizeof(DeltaRecord) * ((baseSize - begSize - endSize) / STRMIN + 50));
// //   *deltaSize = 1024;

//   int offset = (char *)&BaseLink[0].psNextSubCnk - (char *)&BaseLink[0];
// //   htable *psHTable = psHTable_;
// //   psHTable->init(offset, 8, baseSize/16);
// #if POST_DEDUP
//   psHTable->init(offset, 8, (baseSize - begSize - endSize) * 2 / (STRAVG + 1));
// #else
// //   psHTable->init(offset, 8, 8 * 1024);
// #endif

//   // int chunk_length;
//   int flag_chunk = 1; // to tell if basefile has been chunked to the end
//   int numBytes_old;
//   int numBytes_accu = 0; // accumulated numBytes in one turn of chunking the
//                          // base
//   int probe_match; // to tell which chunk for probing matches the some base
//   int flag_handle_probe; // to tell whether the probe chunks need to be handled

//   if (beg) {
//     record1.nOffset = 0;
//     set_length(&record1, begSize);
//     memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
//     deltaLen += sizeof(DeltaUnit1);
//     flag = 1;
//   }

// #define BASE_BEGIN 5
// #define BASE_EXPAND 3
// #define BASE_STEP 3
// #define INPUT_TRY 5

// /* if deltaLen > newSize * RECOMPRESS_THRESHOLD in the first round,we'll
//  * go back to greedy.
//  */
// #define GO_BACK_TO_GREEDY 0

// #define RECOMPRESS_THRESHOLD 0.2

//   DeltaRecord InputLink[INPUT_TRY];
//   // int test=0;

//   while (inputPos < newSize - endSize) {
//     if (flag_chunk) {
//       //		if( cursor_input - input_last_chunk_beg >= numBytes_accu
//       //* 0.8 ){
//       if ((cursor_input / (float)newSize) + 0.5 >=
//           (cursor_base / (float)baseSize)) {
//         numBytes_old = numBytes_accu;
//         numBytes_accu = 0;

//         /* chunk a few chunks in the input first */
//         // Chunking_v3(newBuf+cursor_input, newSize-endSize-cursor_input,
//         // INPUT_TRY, InputLink);
//         flag_handle_probe = 1;
//         probe_match = INPUT_TRY;
//         int chunk_number = BASE_BEGIN;
//         for (int i = 0; i < BASE_STEP; i++) {
//           numBytes = Chunking_v3(
//               baseBuf + cursor_base, baseSize - endSize - cursor_base,
//               chunk_number,
//               BaseLink + numBase); //一个分块base的循环找到 match的就可以跳出

//           for (int j = 0; j < chunk_number; j++) {
//             if (BaseLink[numBase + j].nLength == 0) {
//               flag_chunk = 0;
//               break;
//             }
//             BaseLink[numBase + j].nOffset += cursor_base;
//             psHTable_->emplace(BaseLink[numBase + j].nHash, (DeltaRecord *)&BaseLink[numBase + j]);
//             // psHTable->insert((unsigned char *)&BaseLink[numBase + j].nHash,
//             //                  &BaseLink[numBase + j]);
//           }

//           cursor_base += numBytes;
//           numBase += chunk_number;
//           numBytes_accu += numBytes;

//           chunk_number *= BASE_EXPAND;
//           if (i == 0) {
//             cursor_input1 = cursor_input;
//             for (int j = 0; j < INPUT_TRY; j++) {
//               cursor_input2 = cursor_input1;
//               cursor_input1 = chunk_gear(newBuf + cursor_input2,
//                                          newSize - cursor_input2 - endSize) +
//                               cursor_input2;
//               InputLink[j].nLength = cursor_input1 - cursor_input2;
//               InputLink[j].nHash =
//                   weakHash(newBuf + cursor_input2, InputLink[j].nLength);
//             //   InputLink[j].nHash = 9;
//                 //   weakHash(newBuf + cursor_input2, InputLink[j].nLength);
//             //   if ((psDupSubCnk = (DeltaRecord *)psHTable->lookup(
//             //            (unsigned char *)&(InputLink[j].nHash)))) {
//             //     probe_match = j;
//             //     goto lets_break;
//             //   }
//               res1 = psHTable_->find(InputLink[j].nHash);
//               if (res1 != psHTable_->end())
//               {
//                 psDupSubCnk = res1->second;
//                 probe_match = j;
//                 goto lets_break;
//               }
//             }
//           } else {
//             for (int j = 0; j < INPUT_TRY; j++) {
//               res2 = psHTable_->find(InputLink[j].nHash);
//               if (res2 != psHTable_->end())
//               {
//                 psDupSubCnk = res2->second;
//                 probe_match = j;
//                 goto lets_break;
//               }
//             }
//           }

//           if (flag_chunk == 0)
//           lets_break:
//             break;
//         }

//         input_last_chunk_beg =
//             (cursor_input > (input_last_chunk_beg + numBytes_old)
//                  ? cursor_input
//                  : (input_last_chunk_beg + numBytes_old));
//         // test++;
//       }
//     }

//     // to handle the chunks in input file for probing
//     if (flag_handle_probe) {
//       for (int i = 0; i < INPUT_TRY; i++) {
//         matchsum++;
//         length = InputLink[i].nLength;
//         cursor_input = length + inputPos;

//         if (i == probe_match) 
//         {
//           flag_handle_probe = 0;
//           goto match;
//         } 
//         else 
//         {
//           if (flag == 2) { //把不match的块弄过去
//             /* continuous unique chunks only add unique bytes into the deltaBuf,
//              * but not change the last DeltaUnit2, so the DeltaUnit2 should be
//              * overwritten when flag=1 or at the end of the loop.
//              */
//             memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
//             deltaLen += length;
//             set_length(&record2, get_length(&record2) + length);
//           } else {
//             set_length(&record2, length);

//             memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
//             deltaLen += sizeof(DeltaUnit2);

//             memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
//             deltaLen += length;

//             flag = 2;
//           }

//           inputPos = cursor_input;
//         }
//       }

//       flag_handle_probe = 0;
//     }

//     cursor_input =
//         chunk_gear(newBuf + inputPos, newSize - inputPos - endSize) + inputPos;
//     matchsum++;
//     length = cursor_input - inputPos;
//     hash = weakHash(newBuf + inputPos, length);

//     /* lookup */
//     res3 = psHTable_->find(hash);
//     if (res3 != psHTable_->end())
//     {
//     psDupSubCnk = res3->second;
//     // printf("inputPos: %d length: %d\n", inputPos, length);
//     match:
//       if (length == psDupSubCnk->nLength &&
//           memcmp(newBuf + inputPos, baseBuf + psDupSubCnk->nOffset, length) ==
//               0) {
//         //	printf("match:%d\n",length);
//         match++;
//         if (flag == 2) {
//           /* continuous unique chunks only add unique bytes into the deltaBuf,
//            * but not change the last DeltaUnit2, so the DeltaUnit2 should be
//            * overwritten when flag=1 or at the end of the loop.
//            */
//           memcpy(deltaBuf + deltaLen - get_length(&record2) -
//                      sizeof(DeltaUnit2),
//                  &record2, sizeof(DeltaUnit2));
//         }

//         // greedily detect forward
//         int j = 0;

//         while (psDupSubCnk->nOffset + length + j + 7 < baseSize - endSize &&
//                cursor_input + j + 7 < newSize - endSize) {
//           if (*(uint64_t *)(baseBuf + psDupSubCnk->nOffset + length + j) ==
//               *(uint64_t *)(newBuf + cursor_input + j)) {
//             j += 8;
//           } else
//             break;
//         }
//         while (psDupSubCnk->nOffset + length + j < baseSize - endSize &&
//                cursor_input + j < newSize - endSize) {
//           if (baseBuf[psDupSubCnk->nOffset + length + j] ==
//               newBuf[cursor_input + j]) {
//             j++;
//           } else
//             break;
//         }

//         cursor_input += j;
//         if (psDupSubCnk->nOffset + length + j > cursor_base)
//           cursor_base = psDupSubCnk->nOffset + length + j;

//         set_length(&record1, cursor_input - inputPos);
//         record1.nOffset = psDupSubCnk->nOffset;

//         /* detect backward */
//         uint32_t k = 0;
//         if (flag == 2) {
//           while (k + 1 <= psDupSubCnk->nOffset &&
//                  k + 1 <= get_length(&record2)) {
//             if (baseBuf[psDupSubCnk->nOffset - (k + 1)] ==
//                 newBuf[inputPos - (k + 1)])
//               k++;
//             else
//               break;
//           }
//         }
//         if (k > 0) {
//           deltaLen -= get_length(&record2);
//           deltaLen -= sizeof(DeltaUnit2);

//           set_length(&record2, get_length(&record2) - k);

//           if (get_length(&record2) > 0) {
//             memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
//             deltaLen += sizeof(DeltaUnit2);
//             deltaLen += get_length(&record2);
//           }

//           set_length(&record1, get_length(&record1) + k);
//           record1.nOffset -= k;
//         }

//         memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
//         deltaLen += sizeof(DeltaUnit1);
//         flag = 1;
//       } else {
//         // printf("Spooky Hash Error!!!!!!!!!!!!!!!!!!\n");
//         goto handle_hash_error;
//       }
//     } else {
//     handle_hash_error:
//       //	printf("unmatch:%d\n",length);
//       if (flag == 2) {
//         /* continuous unique chunks only add unique bytes into the deltaBuf,
//          * but not change the last DeltaUnit2, so the DeltaUnit2 should be
//          * overwritten when flag=1 or at the end of the loop.
//          */
//         memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
//         deltaLen += length;
//         set_length(&record2, get_length(&record2) + length);
//       } else {
//         set_length(&record2, length);

//         memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
//         deltaLen += sizeof(DeltaUnit2);

//         memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
//         deltaLen += length;

//         flag = 2;
//       }
//     }

//     inputPos = cursor_input;
//     // printf("cursor_input:%d\n",inputPos);
//   }

//   if (flag == 2) {
//     /* continuous unique chunks only add unique bytes into the deltaBuf,
//      * but not change the last DeltaUnit2, so the DeltaUnit2 should be
//      * overwritten when flag=1 or at the end of the loop.
//      */
//     memcpy(deltaBuf + deltaLen - get_length(&record2) - sizeof(DeltaUnit2),
//            &record2, sizeof(DeltaUnit2));
//   }

//   if (end) {
//     record1.nOffset = baseSize - endSize;
//     set_length(&record1, endSize);
//     memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
//     deltaLen += sizeof(DeltaUnit1);
//   }

// //   if (psHTable) {
// //     free(psHTable->table);
// //     free(psHTable);
// //   }

//     // for(auto kv = psHTable_->begin(); kv != psHTable_->end(); )
//     // {
//     //     uint64_t key = kv->first;
//     //     DeltaRecord* value = kv->second;
//     //     free(value);
//     //     kv = psHTable_->erase(kv);
//     // }

//     psHTable_->clear();

// //   free(BaseLink);
  
// //   deltaLen = 1;
//   *deltaSize = deltaLen;
//   return deltaLen;
// // // *deltaSize = 1024;
// // //   return 1024;
// //   *deltaSize = 1024;
// //   return 1024;
// }

int EcallMeGA::EDeltaEncode(uint8_t *newBuf, uint32_t newSize, uint8_t *baseBuf,
                 uint32_t baseSize, uint8_t *deltaBuf, uint32_t *deltaSize) {
  /* detect the head and tail of one chunk */
  uint32_t beg = 0, end = 0, begSize = 0, endSize = 0;
  float matchsum = 0;
  float match = 0;
  while (begSize + 7 < baseSize && begSize + 7 < newSize) {
    if (*(uint64_t *)(baseBuf + begSize) == *(uint64_t *)(newBuf + begSize)) {
      begSize += 8;
    } else
      break;
  }
  while (begSize < baseSize && begSize < newSize) {
    if (baseBuf[begSize] == newBuf[begSize]) {
      begSize++;
    } else
      break;
  }

  if (begSize > 16)
    beg = 1;
  else
    begSize = 0;

  while (endSize + 7 < baseSize && endSize + 7 < newSize) {
    if (*(uint64_t *)(baseBuf + baseSize - endSize - 8) ==
        *(uint64_t *)(newBuf + newSize - endSize - 8)) {
      endSize += 8;
    } else
      break;
  }
  while (endSize < baseSize && endSize < newSize) {
    if (baseBuf[baseSize - endSize - 1] == newBuf[newSize - endSize - 1]) {
      endSize++;
    } else
      break;
  }

  if (begSize + endSize > newSize)
    endSize = newSize - begSize;

  if (endSize > 16)
    end = 1;
  else
    endSize = 0;
  /* end of detect */

  if (begSize + endSize >= baseSize) {
    DeltaUnit1 record1;
    DeltaUnit2 record2;
    uint32_t deltaLen = 0;
    if (beg) {
      set_flag(&record1, 0);
      record1.nOffset = 0;
      set_length(&record1, begSize);
      memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
      deltaLen += sizeof(DeltaUnit1);
    }
    if (newSize - begSize - endSize > 0) {
      set_flag(&record2, 1);
      set_length(&record2, newSize - begSize - endSize);
      memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
      deltaLen += sizeof(DeltaUnit2);
      memcpy(deltaBuf + deltaLen, newBuf + begSize, get_length(&record2));
      deltaLen += get_length(&record2);
    }
    if (end) {
      set_flag(&record1, 0);
      record1.nOffset = baseSize - endSize;
      set_length(&record1, endSize);
      memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
      deltaLen += sizeof(DeltaUnit1);
    }

    *deltaSize = deltaLen;
    return deltaLen;
  }

  uint32_t deltaLen = 0;
  uint32_t cursor_base = begSize;
  uint32_t cursor_input = begSize;
  uint32_t cursor_input1 = 0;
  uint32_t cursor_input2 = 0;
  uint32_t input_last_chunk_beg = begSize;
  uint32_t inputPos = begSize;
  uint32_t length;
  uint64_t hash;
  DeltaRecord *psDupSubCnk = NULL;
  DeltaUnit1 record1{0, 0};
  DeltaUnit2 record2{0};
  set_flag(&record1, 0);
  set_flag(&record2, 1);
  int flag = 0; /* to represent the last record in the deltaBuf,
       1 for DeltaUnit1, 2 for DeltaUnit2 */

  int numBase = 0;  /* the total number of chunks that the base has chunked */
  int numBytes = 0; /* the number of bytes that the base chunks once */
//   DeltaRecord *BaseLink = (DeltaRecord *)malloc(
//       sizeof(DeltaRecord) * ((baseSize - begSize - endSize) / STRMIN + 50));
  DeltaRecord *BaseLink = BaseLink_;

  int offset = (char *)&BaseLink[0].psNextSubCnk - (char *)&BaseLink[0];
  htable *psHTable = psHTable2_;
  // psHTable->init(offset, 8, baseSize/16);
#if POST_DEDUP
  psHTable->init(offset, 8, (baseSize - begSize - endSize) * 2 / (STRAVG + 1));
#else
  psHTable2_->SetOffset(offset);
//   init(offset, 8, 8 * 1024);
#endif

  // int chunk_length;
  int flag_chunk = 1; // to tell if basefile has been chunked to the end
  int numBytes_old;
  int numBytes_accu = 0; // accumulated numBytes in one turn of chunking the
                         // base
  int probe_match; // to tell which chunk for probing matches the some base
  int flag_handle_probe; // to tell whether the probe chunks need to be handled

  if (beg) {
    record1.nOffset = 0;
    set_length(&record1, begSize);
    memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
    deltaLen += sizeof(DeltaUnit1);
    flag = 1;
  }

#define BASE_BEGIN 5
#define BASE_EXPAND 3
#define BASE_STEP 3
#define INPUT_TRY 5

/* if deltaLen > newSize * RECOMPRESS_THRESHOLD in the first round,we'll
 * go back to greedy.
 */
#define GO_BACK_TO_GREEDY 0

#define RECOMPRESS_THRESHOLD 0.2

  DeltaRecord InputLink[INPUT_TRY];
  // int test=0;

  while (inputPos < newSize - endSize) {
    if (flag_chunk) {
      //		if( cursor_input - input_last_chunk_beg >= numBytes_accu
      //* 0.8 ){
      if ((cursor_input / (float)newSize) + 0.5 >=
          (cursor_base / (float)baseSize)) {
        numBytes_old = numBytes_accu;
        numBytes_accu = 0;

        /* chunk a few chunks in the input first */
        // Chunking_v3(newBuf+cursor_input, newSize-endSize-cursor_input,
        // INPUT_TRY, InputLink);
        flag_handle_probe = 1;
        probe_match = INPUT_TRY;
        int chunk_number = BASE_BEGIN;
        for (int i = 0; i < BASE_STEP; i++) {
          numBytes = Chunking_v3(
              baseBuf + cursor_base, baseSize - endSize - cursor_base,
              chunk_number,
              BaseLink + numBase); //一个分块base的循环找到 match的就可以跳出

          for (int j = 0; j < chunk_number; j++) {
            if (BaseLink[numBase + j].nLength == 0) {
              flag_chunk = 0;
              break;
            }
            BaseLink[numBase + j].nOffset += cursor_base;
            psHTable->insert((unsigned char *)&BaseLink[numBase + j].nHash,
                             &BaseLink[numBase + j]);
          }

          cursor_base += numBytes;
          numBase += chunk_number;
          numBytes_accu += numBytes;

          chunk_number *= BASE_EXPAND;
          if (i == 0) {
            cursor_input1 = cursor_input;
            for (int j = 0; j < INPUT_TRY; j++) {
              cursor_input2 = cursor_input1;
              cursor_input1 = chunk_gear(newBuf + cursor_input2,
                                         newSize - cursor_input2 - endSize) +
                              cursor_input2;
              InputLink[j].nLength = cursor_input1 - cursor_input2;
              InputLink[j].nHash =
                  weakHash(newBuf + cursor_input2, InputLink[j].nLength);
              if ((psDupSubCnk = (DeltaRecord *)psHTable->lookup(
                       (unsigned char *)&(InputLink[j].nHash)))) {
                probe_match = j;
                goto lets_break;
              }
            }
          } else {
            for (int j = 0; j < INPUT_TRY; j++) {
              if ((psDupSubCnk = (DeltaRecord *)psHTable->lookup(
                       (unsigned char *)&(InputLink[j].nHash)))) {
                //printf("find INPUT_TRY: %d BASE_STEP: %d" 
								//	" cursor_input: %d round of chunk: %d\n",
                //	j,i,cursor_input,test);
                probe_match = j;
                goto lets_break;
              }
            }
          }

          if (flag_chunk == 0)
          lets_break:
            break;
        }

        input_last_chunk_beg =
            (cursor_input > (input_last_chunk_beg + numBytes_old)
                 ? cursor_input
                 : (input_last_chunk_beg + numBytes_old));
        // test++;
      }
    }

    // to handle the chunks in input file for probing
    if (flag_handle_probe) {
      for (int i = 0; i < INPUT_TRY; i++) {
        matchsum++;
        length = InputLink[i].nLength;
        cursor_input = length + inputPos;

        if (i == probe_match) {
          flag_handle_probe = 0;
          goto match;
        } else {
          if (flag == 2) { //把不match的块弄过去
            /* continuous unique chunks only add unique bytes into the deltaBuf,
             * but not change the last DeltaUnit2, so the DeltaUnit2 should be
             * overwritten when flag=1 or at the end of the loop.
             */
            memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
            deltaLen += length;
            set_length(&record2, get_length(&record2) + length);
          } else {
            set_length(&record2, length);

            memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
            deltaLen += sizeof(DeltaUnit2);

            memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
            deltaLen += length;

            flag = 2;
          }

          inputPos = cursor_input;
        }
      }

      flag_handle_probe = 0;
    }

    cursor_input =
        chunk_gear(newBuf + inputPos, newSize - inputPos - endSize) + inputPos;
    matchsum++;
    length = cursor_input - inputPos;
    hash = weakHash(newBuf + inputPos, length);

    /* lookup */
    if ((psDupSubCnk =
             (DeltaRecord *)psHTable->lookup((unsigned char *)&hash))) {
    // printf("inputPos: %d length: %d\n", inputPos, length);
    match:
      if (length == psDupSubCnk->nLength &&
          memcmp(newBuf + inputPos, baseBuf + psDupSubCnk->nOffset, length) ==
              0) {
        //	printf("match:%d\n",length);
        match++;
        if (flag == 2) {
          /* continuous unique chunks only add unique bytes into the deltaBuf,
           * but not change the last DeltaUnit2, so the DeltaUnit2 should be
           * overwritten when flag=1 or at the end of the loop.
           */
          memcpy(deltaBuf + deltaLen - get_length(&record2) -
                     sizeof(DeltaUnit2),
                 &record2, sizeof(DeltaUnit2));
        }

        // greedily detect forward
        int j = 0;

        while (psDupSubCnk->nOffset + length + j + 7 < baseSize - endSize &&
               cursor_input + j + 7 < newSize - endSize) {
          if (*(uint64_t *)(baseBuf + psDupSubCnk->nOffset + length + j) ==
              *(uint64_t *)(newBuf + cursor_input + j)) {
            j += 8;
          } else
            break;
        }
        while (psDupSubCnk->nOffset + length + j < baseSize - endSize &&
               cursor_input + j < newSize - endSize) {
          if (baseBuf[psDupSubCnk->nOffset + length + j] ==
              newBuf[cursor_input + j]) {
            j++;
          } else
            break;
        }

        cursor_input += j;
        if (psDupSubCnk->nOffset + length + j > cursor_base)
          cursor_base = psDupSubCnk->nOffset + length + j;

        set_length(&record1, cursor_input - inputPos);
        record1.nOffset = psDupSubCnk->nOffset;

        /* detect backward */
        uint32_t k = 0;
        if (flag == 2) {
          while (k + 1 <= psDupSubCnk->nOffset &&
                 k + 1 <= get_length(&record2)) {
            if (baseBuf[psDupSubCnk->nOffset - (k + 1)] ==
                newBuf[inputPos - (k + 1)])
              k++;
            else
              break;
          }
        }
        if (k > 0) {
          deltaLen -= get_length(&record2);
          deltaLen -= sizeof(DeltaUnit2);

          set_length(&record2, get_length(&record2) - k);

          if (get_length(&record2) > 0) {
            memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
            deltaLen += sizeof(DeltaUnit2);
            deltaLen += get_length(&record2);
          }

          set_length(&record1, get_length(&record1) + k);
          record1.nOffset -= k;
        }

        memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
        deltaLen += sizeof(DeltaUnit1);
        flag = 1;
      } else {
        // printf("Spooky Hash Error!!!!!!!!!!!!!!!!!!\n");
        goto handle_hash_error;
      }
    } else {
    handle_hash_error:
      //	printf("unmatch:%d\n",length);
      if (flag == 2) {
        /* continuous unique chunks only add unique bytes into the deltaBuf,
         * but not change the last DeltaUnit2, so the DeltaUnit2 should be
         * overwritten when flag=1 or at the end of the loop.
         */
        memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
        deltaLen += length;
        set_length(&record2, get_length(&record2) + length);
      } else {
        set_length(&record2, length);

        memcpy(deltaBuf + deltaLen, &record2, sizeof(DeltaUnit2));
        deltaLen += sizeof(DeltaUnit2);

        memcpy(deltaBuf + deltaLen, newBuf + inputPos, length);
        deltaLen += length;

        flag = 2;
      }
    }

    inputPos = cursor_input;
    // printf("cursor_input:%d\n",inputPos);
  }

  if (flag == 2) {
    /* continuous unique chunks only add unique bytes into the deltaBuf,
     * but not change the last DeltaUnit2, so the DeltaUnit2 should be
     * overwritten when flag=1 or at the end of the loop.
     */
    memcpy(deltaBuf + deltaLen - get_length(&record2) - sizeof(DeltaUnit2),
           &record2, sizeof(DeltaUnit2));
  }

  if (end) {
    record1.nOffset = baseSize - endSize;
    set_length(&record1, endSize);
    memcpy(deltaBuf + deltaLen, &record1, sizeof(DeltaUnit1));
    deltaLen += sizeof(DeltaUnit1);
  }

//   if (psHTable) {
//     free(psHTable->table);
//     free(psHTable);
//   }
  psHTable->ResetTable();

//   free(BaseLink);

  *deltaSize = deltaLen;
  return deltaLen;
}

int EcallMeGA::EDeltaDecode(uint8_t *deltaBuf, uint32_t deltaSize, uint8_t *baseBuf,
                 uint32_t baseSize, uint8_t *outBuf, uint32_t *outSize) {

  uint32_t dataLength = 0, readLength = 0;
  int matchnum = 0;
  // int matchlength = 0;
  // int unmatchlength = 0;
  int unmatchnum = 0;
  while (1) {
    u_int32_t flag = get_flag(deltaBuf + readLength);

    if (flag == 0) {
      matchnum++;
      DeltaUnit1 record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnit1));
      readLength += sizeof(DeltaUnit1);
      // matchlength += get_length(&record);
      memcpy(outBuf + dataLength, baseBuf + record.nOffset,
             get_length(&record));

      dataLength += get_length(&record);
    } else {
      unmatchnum++;
      DeltaUnit2 record;
      memcpy(&record, deltaBuf + readLength, sizeof(DeltaUnit2));
      readLength += sizeof(DeltaUnit2);
      // unmatchlength += get_length(&record);
      memcpy(outBuf + dataLength, deltaBuf + readLength, get_length(&record));

      readLength += get_length(&record);
      dataLength += get_length(&record);
    }

    if (readLength >= deltaSize) {
      break;
    }
  }
  *outSize = dataLength;
  return dataLength;
}


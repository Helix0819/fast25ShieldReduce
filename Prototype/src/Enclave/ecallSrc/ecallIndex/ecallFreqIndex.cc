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

#include "../../include/ecallFreqIndex.h"


struct Param
{
    int thread_id;
    unsigned char *ptr;
    EVP_MD_CTX *mdCtx;
    uint8_t *SF;
    EcallCrypto *cryptoObj_;
    int chunksize;
};

struct GetSFTask{
    int begin{0};
    int end{0};
    int thread_id;
};

pthread_mutex_t mutex2;
pthread_t test_pthread[4];

#define INIT(L, R, OFF) L = 0x6c078965 * ((R) ^ (R >> 30u)) + (OFF)

vector<Param> param_list;

int delta_find = 0;
/**
 * @brief Construct a new Ecall Frequency Index object
 * 
 */
EcallFreqIndex::EcallFreqIndex() {


    topThreshold_ = Enclave::topKParam_;
    insideDedupIndex_ = new EcallEntryHeap();
    insideDedupIndex_->SetHeapSize(topThreshold_);
    cmSketch_ = new EcallCMSketch(sketchWidth_, sketchDepth_);
    InContainercache_ = new InContainercache();
    // temp_iv = (uint8_t *)malloc(CRYPTO_BLOCK_SIZE);
    // temp_chunkbuffer = (uint8_t *)malloc(MAX_CHUNK_SIZE);
    // tmp_buffer = (uint8_t *)malloc((8 * 1024 + 8 * 1024) * 2);
    // basechunkbuffer = (uint8_t *)malloc(MAX_CHUNK_SIZE);

    offlinebackOBj_ = new OFFLineBackward();

    if (ENABLE_SEALING) {
        if (!this->LoadDedupIndex()) {
            Enclave::Logging(myName_.c_str(), "do not need to load the index.\n");
        }
    }
    Enclave::Logging(myName_.c_str(), "init the EcallFreqIndex.\n");
}

/**
 * @brief Destroy the Ecall Frequency Index object
 * 
 */
EcallFreqIndex::~EcallFreqIndex() {

    if (ENABLE_SEALING) {
        this->PersistDedupIndex();
    }
    delete insideDedupIndex_;
    delete cmSketch_;
    delete InContainercache_;
    delete offlinebackOBj_;
    // free(temp_iv);
    // free(temp_chunkbuffer);
    // free(tmp_buffer);
    // free(basechunkbuffer);

    Enclave::Logging(myName_.c_str(), "========EcallFreqIndex Info========\n");
    Enclave::Logging(myName_.c_str(), "logical chunk num: %lu\n", _logicalChunkNum);
    Enclave::Logging(myName_.c_str(), "logical data size: %lu\n", _logicalDataSize);
    Enclave::Logging(myName_.c_str(), "unique chunk num: %lu\n", _uniqueChunkNum);
    Enclave::Logging(myName_.c_str(), "unique data size: %lu\n", _uniqueDataSize);
    Enclave::Logging(myName_.c_str(), "base chunk num: %lu\n", _baseChunkNum);
    Enclave::Logging(myName_.c_str(), "base chunk num: %lu\n", _baseDataSize);
    Enclave::Logging(myName_.c_str(), "delta chunk num: %lu\n", _deltaChunkNum);
    Enclave::Logging(myName_.c_str(), "delta chunk size: %lu\n", _deltaDataSize);
    Enclave::Logging(myName_.c_str(),"FPIncall :%d, SFIncall :%d, LocalIncall:%d, LoadIncall:%d, DeltaIncall:%d, RecipeIncall: %d\n",_Inline_FPOcall,_Inline_SFOcall,_Inline_LocalOcall,_Inline_LoadOcall,_Inline_DeltaOcall,_Inline_RecipeOcall);
    // Enclave::Logging(myName_.c_str(), "inside dedup chunk num: %lu\n", insideDedupChunkNum_);
    // Enclave::Logging(myName_.c_str(), "inside dedup data size: %lu\n", insideDedupDataSize_);
    Enclave::Logging(myName_.c_str(), "===================================\n");
}

uint32_t* EcallFreqIndex::mt_n_get(uint32_t seed, int n) {
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

void EcallFreqIndex::getSF2(unsigned char *ptr, EVP_MD_CTX *mdCtx, uint8_t *SF, EcallCrypto *cryptoObj_, int chunkSize) {
    
 
  EVP_MD_CTX *mdCtx_ = mdCtx;
  int BLOCK_SIZE;

  BLOCK_SIZE = chunkSize;

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

void* EcallFreqIndex::GetSF_thread_func_f(void* arg){
    auto info = *(GetSFTask*)arg;
    for(int i = info.begin ; i < info.end; i++){
        auto param = param_list[i];
        if(param.SF){
            getSF2(param.ptr,param.mdCtx,param.SF,param.cryptoObj_,param.chunksize);
        }
    }
    return NULL;
}



void EcallFreqIndex::getSF(unsigned char *ptr, EVP_MD_CTX *mdCtx, uint8_t *SF, EcallCrypto *cryptoObj_)
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
void EcallFreqIndex::UpdateInsideIndexFreq(const string& chunkFp, uint32_t currentFreq) {
    insideDedupIndex_->Update(chunkFp, currentFreq);
    return ;
}

/**
 * @brief process the tailed batch when received the end of the recipe flag
 * 
 * @param upOutSGX the pointer to enclave-related var
 */
void EcallFreqIndex::ProcessTailBatch(UpOutSGX_t* upOutSGX) {
    // the in-enclave info
    EnclaveClient *sgxClient = (EnclaveClient *)upOutSGX->sgxClient;
    Recipe_t *inRecipe = &sgxClient->_inRecipe;
    Recipe_t *outRecipe = (Recipe_t *)upOutSGX->outRecipe;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t *masterKey = sgxClient->_masterKey;
    
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
    return;
}

/**
 * @brief persist the deduplication index into the disk
 * 
 * @return true success
 * @return false fail
 */
bool EcallFreqIndex::PersistDedupIndex() {
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
        Ocall_SGX_Exit_Error("EcallFreqIndex: cannot init the sketch sealed file.");
    }

    for (size_t i = 0; i < sketchDepth_; i++) {
        Enclave::WriteBufferToFile((uint8_t*)counterArrary[i], sketchWidth_ * sizeof(uint32_t), SEALED_SKETCH);
    }
    Ocall_CloseWriteSealedFile(SEALED_SKETCH);

    // step-2: persist the min-heap 
    offset = 0;
    Ocall_InitWriteSealedFile(&persistenceStatus, SEALED_FREQ_INDEX);
    if (persistenceStatus == false) {
        Ocall_SGX_Exit_Error("EcallFreqIndex: cannot init the heap sealed file.");
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
bool EcallFreqIndex::LoadDedupIndex() {
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
bool EcallFreqIndex::CheckIfAddToHeap(uint32_t chunkFreq) {
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
void EcallFreqIndex::AddChunkToHeap(uint32_t chunkFreq, RecipeEntry_t* chunkAddr, 
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

#if (IMPACT_OF_TOP_K == 0)

/**
 * @brief process one batch
 * 
 * @param recvChunkBuf the recv chunk buffer
 * @param upOutSGX the pointer to the enclave-related var 
 */
void EcallFreqIndex::ProcessOneBatch(SendMsgBuffer_t* recvChunkBuf, 
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

    // tmp var
    OutQueryEntry_t* outQueryEntry = outQueryBase;
    uint32_t outQueryNum = 0;
    string tmpHashStr;
    tmpHashStr.resize(CHUNK_HASH_SIZE, 0);

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
                    Ocall_SGX_Exit_Error("EcallFreqIndex: wrong in-enclave dedup flag");
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

    
    // check the out-enclave index
    if (outQueryNum != 0) {
        upOutSGX->outQuery->queryNum = outQueryNum;
        Ocall_QueryOutIndex(upOutSGX->outClient);
        _Inline_Ocall++;
        _Inline_FPOcall++;
    }

    //为每一个Unique chunk计算superfeature
    inQueryEntry = inQueryBase;
    outQueryEntry = upOutSGX->outQuery->outQueryBase;
    currentOffset = 0;
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

    //OFFLINE
    int batch_out_times = 0;
    bool Local_Flag = 0;
    vector<pair<string,string>> batch_basemap;

#if(SF_SINGLE_THREAD == 0)

    OutEntrySFGet(inQueryBase,outQueryBase,chunkNum);

#endif

    Ocall_QueryOutBasechunk(upOutSGX->outClient);
    if(outQueryNum != 0){
       _Inline_Ocall++;
       _Inline_SFOcall++;
    }


    Local_Flag = LocalChecker(inQueryBase,outQueryBase,upOutSGX,chunkNum);
    
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

                        OfflinedeltaTure(inQueryEntry,outQueryEntry,upOutSGX,batch_basemap,batch_out_times,Local_Flag);

                        uint8_t *deltachunk;
                        size_t deltachunk_size;
                        
                        if (outQueryEntry->deltaFlag == DELTA)
                        {
                            deltachunk = ProcessDeltachunk(inQueryEntry,outQueryEntry,upOutSGX,currentOffset,recvBuffer,&deltachunk_size);
                        }

                        if(inQueryEntry->chunkAddr.deltaFlag == DELTA){
                            outQueryEntry->deltaFlag = DELTA;
                            this->ProcessSheUniqueChunk(&inQueryEntry->chunkAddr,deltachunk,deltachunk_size, upOutSGX, (uint8_t *)&inQueryEntry->superfeature, (uint8_t *)&outQueryEntry->chunkHash);
                            free(deltachunk);
                            string baseChunkHash;
                            string deltaChunkHash;
                            baseChunkHash.assign((char*)&outQueryEntry->chunkAddr.basechunkHash[0], CHUNK_HASH_SIZE);
                            deltaChunkHash.assign((char*)&outQueryEntry->chunkHash[0], CHUNK_HASH_SIZE);
                            memset(upOutSGX->process_buffer, 0, 1000 * CHUNK_HASH_SIZE);
                            memcpy(upOutSGX->process_buffer, (uint8_t*)&baseChunkHash[0], CHUNK_HASH_SIZE);
                            memcpy(upOutSGX->process_buffer + CHUNK_HASH_SIZE, (uint8_t*)&deltaChunkHash[0], CHUNK_HASH_SIZE);
                            Ocall_UpdateDeltaIndex(upOutSGX->outClient);
                            _Inline_Ocall++;
                            _Inline_DeltaOcall++;
                            _deltaChunkNum++;
                            _deltaDataSize += inQueryEntry->chunkAddr.length;
                            _DeltaSaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                            _Online_DeltaSaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);


                        }else if (inQueryEntry->chunkAddr.deltaFlag == NO_DELTA){
                            outQueryEntry->deltaFlag = NO_DELTA;
                            this->ProcessSheUniqueChunk(&inQueryEntry->chunkAddr,recvBuffer + currentOffset, tmpChunkSize, upOutSGX, (uint8_t *)&inQueryEntry->superfeature, (uint8_t *)&outQueryEntry->chunkHash);

                            _baseChunkNum++;
                            _baseDataSize += inQueryEntry->chunkAddr.length;
                            _lz4SaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                        }

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
                        Ocall_SGX_Exit_Error("EcallFreqIndex: wrong out-enclave dedup flag");
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
                Ocall_SGX_Exit_Error("EcallFreqIndex: wrong chunk status flag");
            }
        }
        this->UpdateFileRecipe(tmpChunkAddr, inRecipe,upOutSGX,&inQueryEntry->chunkHash[0]);


        inQueryEntry++;
        currentOffset += tmpChunkSize;

        // update the statistic
        _logicalDataSize += tmpChunkSize;
        _logicalChunkNum++;
    }


    if(Local_Flag){
        for (int i = 0;i < batch_basemap.size();i++)
        {
            uint8_t* buffer = upOutSGX->test_buffer;
            memcpy(buffer,(uint8_t*)&(batch_basemap[i].first[0]),CHUNK_HASH_SIZE);
            memcpy(buffer+CHUNK_HASH_SIZE,(uint8_t*)&(batch_basemap[i].second[0]),CHUNK_HASH_SIZE);
            Ocall_LocalInsert(upOutSGX->outClient);
        }
        _Inline_Ocall++;
        _Inline_LocalOcall++;
    }


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


    return ;
}

#else

/**
 * @brief process one batch (breakdown version)
 * 
 * @param recvChunkBuf the recv chunk buffer
 * @param upOutSGX the pointer to the enclave-related var 
 */
void EcallFreqIndex::ProcessOneBatch(SendMsgBuffer_t* recvChunkBuf, 
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

    // tmp var
    OutQueryEntry_t* outQueryEntry = outQueryBase;
    uint32_t outQueryNum = 0;
    string tmpHashStr;
    tmpHashStr.resize(CHUNK_HASH_SIZE, 0);

#if (EDR_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_startTime);
#endif

    // decrypt the received data with the session key
    cryptoObj_->SessionKeyDec(cipherCtx, recvChunkBuf->dataBuffer,
        recvChunkBuf->header->dataSize, sessionKey, recvBuffer);

#if (EDR_BREAKDOWN == 1)
    Ocall_GetCurrentTime(&_endTime);
    _dataTransTime += (_endTime - _startTime);
    _dataTransCount++;
#endif
    
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

#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_startTime);
#endif
        cryptoObj_->GenerateHash(mdCtx, recvBuffer + currentOffset,
            inQueryEntry->chunkSize, inQueryEntry->chunkHash);

#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_endTime);
        _fingerprintTime += (_endTime - _startTime);
        _fingerprintCount++;
#endif
        currentOffset += inQueryEntry->chunkSize;
        inQueryEntry++;
    }


    // update the sketch and freq
    inQueryEntry = inQueryBase;
    for (size_t i = 0; i < chunkNum; i++) {

#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_startTime);
#endif

        cmSketch_->Update(inQueryEntry->chunkHash, CHUNK_HASH_SIZE, 1);
        inQueryEntry->chunkFreq = cmSketch_->Estimate(inQueryEntry->chunkHash,
            CHUNK_HASH_SIZE);

#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_endTime);
        _freqTime += (_endTime - _startTime);
        _freqCount++;
#endif

        inQueryEntry++;
    }


#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_startTime);
#endif

    // check the top-k index
    uint32_t minFreq;
    if (insideDedupIndex_->Size() == topThreshold_) {
        minFreq = insideDedupIndex_->TopEntry();
    } else {
        minFreq = 0;
    }

#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_endTime);
        _firstDedupTime += (_endTime - _startTime);
        _firstDedupCount++;
#endif

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
                    Ocall_SGX_Exit_Error("EcallFreqIndex: wrong in-enclave dedup flag");
                }
            }

            // update the freq
            findEntry->chunkFreq = inQueryEntry->chunkFreq;
        } else {
            // it does not exists in the batch index, compare the freq 
            if (inQueryEntry->chunkFreq < minFreq) {
                // its frequency is smaller than the minimum value in the heap, must not exist in the heap
                // encrypt its fingerprint, write to the outside buffer
#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif

                cryptoObj_->IndexAESCMCEnc(cipherCtx, inQueryEntry->chunkHash,
                    CHUNK_HASH_SIZE, Enclave::indexQueryKey_, outQueryEntry->chunkHash);

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _secondDedupTime += (_endTime - _startTime);
            _secondDedupCount++;
#endif

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

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif
                    cryptoObj_->IndexAESCMCEnc(cipherCtx, inQueryEntry->chunkHash, CHUNK_HASH_SIZE,
                        Enclave::indexQueryKey_, outQueryEntry->chunkHash);

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _secondDedupTime += (_endTime - _startTime);
            _secondDedupCount++;
#endif

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

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif
    // check the out-enclave index
    if (outQueryNum != 0) {
        upOutSGX->outQuery->queryNum = outQueryNum;
        Ocall_QueryOutIndex(upOutSGX->outClient);
    }

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _secondDedupTime += (_endTime - _startTime);
            _secondDedupCount++;
#endif

    //为每一个Unique chunk计算superfeature
    inQueryEntry = inQueryBase;
    outQueryEntry = upOutSGX->outQuery->outQueryBase;
    currentOffset = 0;
    for(size_t i = 0; i < chunkNum; i++){
        currentOffset += sizeof(uint32_t);
        if(inQueryEntry->dedupFlag == UNIQUE){
            if(outQueryEntry->dedupFlag == UNIQUE){

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif
                getSF2(recvBuffer + currentOffset,mdCtx,(uint8_t *)&inQueryEntry->superfeature,this->cryptoObj_,inQueryEntry->chunkSize);

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _superfeatureTime  += (_endTime - _startTime);
            _superfeatureCount++;
#endif
                outQueryEntry->deltaFlag = NO_DELTA;
                outQueryEntry->offlineFlag = 0;
                memcpy(&outQueryEntry->superfeature, &inQueryEntry->superfeature, 3 * CHUNK_HASH_SIZE);

            }
            outQueryEntry++;
        }
        currentOffset += inQueryEntry->chunkSize;
        inQueryEntry++;
    }

    //OFFLINE
    int batch_out_times = 0;
    bool Local_Flag = 0;
    unordered_map<string,string> batch_basemap;

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif

    Ocall_QueryOutBasechunk(upOutSGX->outClient);

    Local_Flag = LocalChecker(inQueryBase,outQueryBase,upOutSGX,chunkNum);

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _localcheckerTime  += (_endTime - _startTime);
            _localcheckerCount++;
#endif
    
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

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif
                        // it is duplicate for the out-enclave index
                        cryptoObj_->AESCBCDec(cipherCtx, (uint8_t*)&outQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t), Enclave::indexQueryKey_,
                            (uint8_t*)&inQueryEntry->chunkAddr);
#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _secondDedupTime  += (_endTime - _startTime);
            _secondDedupCount++;
#endif
                        tmpChunkAddr.assign((char*)&inQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t));
                        break;
                    }
                    case UNIQUE: {
                        // it also unique for the out-enclave index
                        inQueryEntry->deltaFlag = NO_DELTA;
                        inQueryEntry->chunkAddr.deltaFlag = NO_DELTA;

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif
                        OfflinedeltaTure(inQueryEntry,outQueryEntry,upOutSGX,batch_basemap,batch_out_times,Local_Flag);

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _deltacompressTime  += (_endTime - _startTime);
            _deltacompressCount++;
#endif

                        uint8_t *deltachunk;
                        size_t deltachunk_size;
                        
                        if (outQueryEntry->deltaFlag == DELTA)
                        {

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_startTime);
#endif
                            deltachunk = ProcessDeltachunk(inQueryEntry,outQueryEntry,upOutSGX,currentOffset,recvBuffer,&deltachunk_size);

#if (EDR_BREAKDOWN == 1)
            Ocall_GetCurrentTime(&_endTime);
            _deltacompressTime  += (_endTime - _startTime);
            _deltacompressCount++;
#endif
                        }

                        if(inQueryEntry->chunkAddr.deltaFlag == DELTA){
                            outQueryEntry->deltaFlag = DELTA;
                            this->ProcessSheUniqueChunk(&inQueryEntry->chunkAddr,deltachunk,deltachunk_size, upOutSGX, (uint8_t *)&inQueryEntry->superfeature, (uint8_t *)&outQueryEntry->chunkHash);
                            free(deltachunk);
                            string baseChunkHash;
                            string deltaChunkHash;
                            baseChunkHash.assign((char*)&outQueryEntry->chunkAddr.basechunkHash[0], CHUNK_HASH_SIZE);
                            deltaChunkHash.assign((char*)&outQueryEntry->chunkHash[0], CHUNK_HASH_SIZE);
                            memset(upOutSGX->process_buffer, 0, 1000 * CHUNK_HASH_SIZE);
                            memcpy(upOutSGX->process_buffer, (uint8_t*)&baseChunkHash[0], CHUNK_HASH_SIZE);
                            memcpy(upOutSGX->process_buffer + CHUNK_HASH_SIZE, (uint8_t*)&deltaChunkHash[0], CHUNK_HASH_SIZE);
                            _deltaChunkNum++;
                            _deltaDataSize += inQueryEntry->chunkAddr.length;
                            _DeltaSaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                            _Online_DeltaSaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);


#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_startTime);
#endif
                            Ocall_UpdateDeltaIndex(upOutSGX->outClient);

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_endTime);
                _localcheckerTime += (_endTime - _startTime);
                _localcheckerCount++;
#endif


                        }else if (inQueryEntry->chunkAddr.deltaFlag == NO_DELTA){
                            outQueryEntry->deltaFlag = NO_DELTA;
                            this->ProcessSheUniqueChunk(&inQueryEntry->chunkAddr,recvBuffer + currentOffset, tmpChunkSize, upOutSGX, (uint8_t *)&inQueryEntry->superfeature, (uint8_t *)&outQueryEntry->chunkHash);
                            _baseChunkNum++;
                            _baseDataSize += inQueryEntry->chunkAddr.length;
                            _lz4SaveSize += (tmpChunkSize - inQueryEntry->chunkAddr.length);
                        }

                        tmpChunkAddr.assign((char*)&inQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t));

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_startTime);
#endif
                        // encrypt the chunk address, write to the out-enclave buffer
                        cryptoObj_->AESCBCEnc(cipherCtx, (uint8_t*)&inQueryEntry->chunkAddr,
                            sizeof(RecipeEntry_t), Enclave::indexQueryKey_,
                            (uint8_t*)&outQueryEntry->chunkAddr);

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_endTime);
                _secondDedupTime += (_endTime - _startTime);
                _secondDedupCount++;
#endif

                        string chunkciperhash;
                        chunkciperhash.assign((char*)outQueryEntry->chunkHash,CHUNK_HASH_SIZE);
                        string chunksuperfeature;
                        chunksuperfeature.assign((char*)outQueryEntry->superfeature,3*CHUNK_HASH_SIZE);
                        uint8_t* chunkciperrecipebuffer;
                        chunkciperrecipebuffer = (uint8_t*)malloc(sizeof(RecipeEntry_t));
                        memcpy(chunkciperrecipebuffer,(uint8_t*)&outQueryEntry->chunkAddr,sizeof(RecipeEntry_t));
                        uint8_t* chunkciperhashbuffer;
                        chunkciperhashbuffer = (uint8_t*)malloc(CHUNK_HASH_SIZE);
                        memcpy(chunkciperhashbuffer,(uint8_t*)&outQueryEntry->chunkHash[0],CHUNK_HASH_SIZE);

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_startTime);
#endif
                        this->UpdateIndexStore(chunkciperhash,(char*)chunkciperrecipebuffer,sizeof(RecipeEntry_t));

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_endTime);
                _secondDedupTime += (_endTime - _startTime);
                _secondDedupCount++;
#endif

                        if(outQueryEntry->deltaFlag == NO_DELTA &&outQueryEntry->offlineFlag == 0){

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_startTime);
#endif

                        this->UpdateIndexSF(chunksuperfeature,(char*)chunkciperhashbuffer,CHUNK_HASH_SIZE);

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_endTime);
                _deltacompressTime += (_endTime - _startTime);
                _deltacompressCount++;
#endif

                }
                        free(chunkciperrecipebuffer);
                        free(chunkciperhashbuffer);


                        // update the statistic
                        _uniqueChunkNum++;
                        _uniqueDataSize += tmpChunkSize;
                        break;
                    }
                    default: {
                        Ocall_SGX_Exit_Error("EcallFreqIndex: wrong out-enclave dedup flag");
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
                Ocall_SGX_Exit_Error("EcallFreqIndex: wrong chunk status flag");
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

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_startTime);
#endif

    if(Local_Flag){
        for (unordered_map<string, string>::iterator it = batch_basemap.begin(); it != batch_basemap.end(); it++)
        {

            uint8_t* buffer = upOutSGX->test_buffer;
            if(buffer == nullptr){
                Enclave::Logging("DE BUG","NULLLLLLLLLLLLLLL\n");
            }
            memcpy(buffer,(uint8_t*)&(it->first[0]),CHUNK_HASH_SIZE);
            memcpy(buffer+CHUNK_HASH_SIZE,(uint8_t*)&(it->second[0]),CHUNK_HASH_SIZE);
            Ocall_LocalInsert(upOutSGX->outClient);
            //offlinebackOBj_->Insert_local(it->first, it->second);
        }
    }

#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_endTime);
                _localcheckerTime += (_endTime - _startTime);
                _localcheckerCount++;
#endif

#if (EDR_BREAKDOWN == 1)
        Ocall_GetCurrentTime(&_startTime);
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
#if (EDR_BREAKDOWN == 1)
                Ocall_GetCurrentTime(&_endTime);
                _firstDedupTime += (_endTime - _startTime);
                _firstDedupCount++;
#endif


#if (EDR_BREAKDOWN == 1)
     Ocall_GetCurrentTime(&_startTime);
#endif
#if (EDR_BREAKDOWN == 1)
     Ocall_GetCurrentTime(&_endTime);
    _testOCallTime += (_endTime - _startTime);
     _testOCallCount++;
#endif
    // update the out-enclave index
    upOutSGX->outQuery->queryNum = outQueryNum;
    upOutSGX->outQuery->currNum = 0;
    sgxClient->_localIndex.clear();


    return ;
}

#endif



void EcallFreqIndex::EntryLoad(InQueryEntry_t *_inQueryEntry, OutQueryEntry_t *_outQueryEntry, UpOutSGX_t *_upOutSGX,set<string> &Batch_ContainerIDset){
   EnclaveClient *sgxClient = (EnclaveClient *)_upOutSGX->sgxClient;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t* tmpcontainer =  _upOutSGX->outcallcontainer;

    if (_outQueryEntry->deltaFlag == OUT_DELTA)
    {
    // copy the hash of the base chunk address from the output container to the input container
    memcpy(&_inQueryEntry->chunkAddr.basechunkHash, &_outQueryEntry->chunkAddr.basechunkHash, CHUNK_HASH_SIZE);
    // decrypt the base chunk address from the output container to the input container
    cryptoObj_->AESCBCDec(cipherCtx, (uint8_t *)&_outQueryEntry->basechunkAddr, sizeof(RecipeEntry_t), Enclave::indexQueryKey_, (uint8_t *)&_inQueryEntry->basechunkAddr);
    // copy the decrypted base chunk address from the output container to the input container
    memcpy(&_outQueryEntry->basechunkAddr,&_inQueryEntry->basechunkAddr,sizeof(RecipeEntry_t));

    // copy the container name from the output container to the input container
    string ContainerIDstr;
    ContainerIDstr.resize(CONTAINER_ID_LENGTH,0);
    ContainerIDstr.assign((char*)_inQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);
    // add the container name to the set of container ids
    Batch_ContainerIDset.insert(ContainerIDstr);
    }
    return;
}


bool EcallFreqIndex::LocalChecker(InQueryEntry_t *_inQueryBase, OutQueryEntry_t *_outQueryBase, UpOutSGX_t *_upOutSGX,uint32_t _chunkNum){
   InQueryEntry_t *inQueryEntry = _inQueryBase;
    OutQueryEntry_t *outQueryEntry = _outQueryBase;
    set<string> Batch_ContainerIDset;
    for(size_t i = 0; i < _chunkNum; i++){
        //if the input entry is unique, we need to check if the output entry is also unique
        if(inQueryEntry->dedupFlag == UNIQUE){
            //if the output entry is also unique, we need to check if the output entry is full
            if(outQueryEntry->dedupFlag == UNIQUE){
                //if the output entry is full, we need to load the input entry into the output entry
                EntryLoad(inQueryEntry,outQueryEntry,_upOutSGX,Batch_ContainerIDset);
            }
            //if the output entry is not full, we need to move the output entry to the next position
            outQueryEntry++;
        }
        //move to the next input entry
        inQueryEntry++;
    }


    //check if the batch is full
    int thresold;
    if(Forward_Flag == true){
        //if the batch is forward, we need to check the number of unique containers
        thresold = _chunkNum * 128;
    }else{
        //if the batch is backward, we need to check the number of unique containers
        thresold = _chunkNum * OFFLINE_THRESHOLD;
    }
    

    //if the batch is full, we need to return true
    if(Batch_ContainerIDset.size() >  thresold){
        return true;
    }else{
        //if the batch is not full, we need to return false
        return false;
    }
}

void EcallFreqIndex::OfflinedeltaTure(InQueryEntry_t *_inQueryEntry, OutQueryEntry_t *_outQueryEntry, UpOutSGX_t *_upOutSGX,vector<pair<string,string>> &_batch_map,int &_batch_out_times,bool Local_Flag)
{
    int container_flag = 1;
    EnclaveClient *sgxClient = (EnclaveClient *)_upOutSGX->sgxClient;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t* tmpcontainer =  _upOutSGX->outcallcontainer;

    // if the first three bytes of the chunk hash is 161, 77, 140, then it is not delta
    if(_inQueryEntry->chunkHash[0] == 161 && _inQueryEntry->chunkHash[1] == 77 &&_inQueryEntry->chunkHash[2]==140){
        _outQueryEntry->deltaFlag = NO_DELTA;
        return;
    }


    // if the delta flag is out delta, then we need to check if the chunk is already in the cache

    if(Local_Flag&&_outQueryEntry->deltaFlag == OUT_DELTA){
        string tmpNewchunkStr;
        tmpNewchunkStr.resize(CHUNK_HASH_SIZE,0);
        tmpNewchunkStr.assign((char*)_outQueryEntry->chunkHash,CHUNK_HASH_SIZE);

        string tmpOldchunkStr;
        tmpOldchunkStr.resize(CHUNK_HASH_SIZE,0);
        tmpOldchunkStr.assign((char*)_outQueryEntry->chunkAddr.basechunkHash,CHUNK_HASH_SIZE);
        pair<string,string> tmppair;
        tmppair = {tmpOldchunkStr,tmpNewchunkStr};
        _batch_map.push_back(tmppair);
        _outQueryEntry->deltaFlag = NO_DELTA;

        // set the offline flag to 1
        _outQueryEntry->offlineFlag = 1;

        // increment the delta_find
        delta_find++;
       
        return;
    }


    // if the delta flag is in delta, then we need to check if the chunk is already in the cache

    if(_outQueryEntry->deltaFlag == OUT_DELTA){

    string tmpContainerIDStr;
    tmpContainerIDStr.assign((char *)_outQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);
    if(InContainercache_->ExistsInCache(tmpContainerIDStr)){
        _outQueryEntry->containerbuffer = InContainercache_->ReadFromCache(tmpContainerIDStr);
        memcpy(&_inQueryEntry->chunkAddr.basechunkHash, &_outQueryEntry->chunkAddr.basechunkHash, CHUNK_HASH_SIZE);
        _outQueryEntry->deltaFlag = DELTA;


        return; 
    }
    }

    // if the delta flag is in delta, then we need to check if the chunk is already in the cache
#if(MULTI_CLIENT == 1)
    Enclave::inContainerLck_.lock();
#endif

    // if the delta flag is in delta, then we need to check if the chunk is already in the cache
    if (_outQueryEntry->deltaFlag == OUT_DELTA)
    {
        string tmpContainerIDStr_1;
        tmpContainerIDStr_1.assign((char *)_outQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);
        Ocall_getRefContainer(_upOutSGX->outClient);
        _Inline_Ocall++;
        _Inline_LoadOcall++;
  
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

                memcpy(&_inQueryEntry->chunkAddr.basechunkHash, &_outQueryEntry->chunkAddr.basechunkHash, CHUNK_HASH_SIZE);

                _batch_out_times++;

            }
        }
    }
#if(MULTI_CLIENT == 1)
    Enclave::inContainerLck_.unlock();
#endif
    return; 

}

uint8_t* EcallFreqIndex::ProcessDeltachunk(InQueryEntry_t *inQueryEntry, OutQueryEntry_t *outQueryEntry, UpOutSGX_t *upOutSGX,size_t currentOffset,uint8_t* recvBuffer, size_t *deltachunk_size)
{
    uint8_t *reccchunk;
    uint8_t* deltachunk;
    size_t reccchunk_size;
    int refchunksize;
    uint8_t temp_decompressChunk[MAX_CHUNK_SIZE];
    EnclaveClient *sgxClient = (EnclaveClient *)upOutSGX->sgxClient;
    EVP_CIPHER_CTX *cipherCtx = sgxClient->_cipherCtx;
    uint8_t* tmpbuffer;
    string tmpContainerIDStr_1;
    tmpContainerIDStr_1.assign((char *)inQueryEntry->basechunkAddr.containerName, CONTAINER_ID_LENGTH);

    uint8_t *temp_iv = (uint8_t *)malloc(CRYPTO_BLOCK_SIZE);
    uint8_t *temp_chunkbuffer = (uint8_t *)malloc(MAX_CHUNK_SIZE);
    uint8_t *tmp_buffer = (uint8_t *)malloc((8 * 1024 + 8 * 1024) * 2);
    uint8_t *basechunkbuffer = (uint8_t *)malloc(MAX_CHUNK_SIZE);

    bool result = InContainercache_->ExistsInCache(tmpContainerIDStr_1);
    if(result == 0){
        Enclave::Logging(myName_.c_str(), "IScontainer:%d\n",result);
        outQueryEntry->deltaFlag = NO_DELTA;
        return nullptr;
    }
    tmpbuffer = outQueryEntry->containerbuffer; 
    memcpy(temp_chunkbuffer, tmpbuffer + inQueryEntry->basechunkAddr.offset + sizeof(RecipeEntry_t) + 4 * CHUNK_HASH_SIZE, inQueryEntry->basechunkAddr.length);


    memcpy(temp_iv, tmpbuffer + inQueryEntry->basechunkAddr.offset + inQueryEntry->basechunkAddr.length + sizeof(RecipeEntry_t) + 4 * CHUNK_HASH_SIZE, CRYPTO_BLOCK_SIZE);


    // Decrypt basechunk with iv-key

    cryptoObj_->DecryptionWithKeyIV(cipherCtx, temp_chunkbuffer, outQueryEntry->basechunkAddr.length, Enclave::enclaveKey_, temp_decompressChunk, temp_iv);
 
    // Decode basechunk by lz4

    refchunksize = LZ4_decompress_safe((char *)temp_decompressChunk, (char *)basechunkbuffer, outQueryEntry->basechunkAddr.length, MAX_CHUNK_SIZE);

    // do delta compression
    if (refchunksize > 0)
    {
        deltachunk = xd3_encode(recvBuffer + currentOffset, inQueryEntry->chunkSize, basechunkbuffer, refchunksize, deltachunk_size,tmp_buffer);
        
        // reccchunk = xd3_decode(deltachunk, *deltachunk_size, basechunkbuffer, refchunksize, &reccchunk_size);
        if (*deltachunk_size >= inQueryEntry->chunkSize)
        {
            outQueryEntry->deltaFlag = NO_DELTA;
            free(deltachunk);
        }
        // free(reccchunk);
    }
    else
    {
        deltachunk = xd3_encode(recvBuffer + currentOffset, inQueryEntry->chunkSize, temp_decompressChunk, outQueryEntry->basechunkAddr.length, deltachunk_size,tmp_buffer);
        if (*deltachunk_size >= inQueryEntry->chunkSize)
        {
            outQueryEntry->deltaFlag = NO_DELTA;
            free(deltachunk);
        }
    }

    inQueryEntry->chunkAddr.deltaFlag = outQueryEntry->deltaFlag;
    free(temp_iv);
    free(temp_chunkbuffer);
    free(tmp_buffer);
    free(basechunkbuffer);
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
uint8_t *EcallFreqIndex::xd3_encode(uint8_t *in, size_t in_size, uint8_t *ref, size_t ref_size, size_t *res_size, uint8_t *tmpbuffer) // 更改函数
{
    size_t sz;
    uint32_t sz32;
    uint32_t insize32 = in_size;
    uint32_t refsize32 = ref_size;
    int resedelta = EDeltaEncode(in, in_size, ref, ref_size, tmpbuffer, &sz32);
    uint8_t *res;
    sz = sz32;
    res = (uint8_t *)malloc(sz);
    *res_size = sz;
    memcpy(res, tmpbuffer, sz);
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
// uint8_t *EcallFreqIndex::xd3_decode(const uint8_t *in, size_t in_size, const uint8_t *ref, size_t ref_size, size_t *res_size) // 更改函数
// {
//     const auto max_buffer_size = (in_size + ref_size) * 2;
//     uint8_t *buffer;
//     buffer = (uint8_t *)malloc(max_buffer_size);
//     size_t sz;
//     xd3_decode_memory(in, in_size, ref, ref_size, buffer, &sz, max_buffer_size, 0);
//     uint8_t *res;
//     res = (uint8_t *)malloc(sz);
//     *res_size = sz;
//     memcpy(res, buffer, sz);
//     free(buffer);
//     return res;
// }



void EcallFreqIndex::ProcessOffline(SendMsgBuffer_t *recvChunkBuf, UpOutSGX_t *upOutSGX)
{
   offlinebackOBj_->_offlineCompress_size = _onlineCompress_size;
    offlinebackOBj_->_offlineCurrBackup_size = _onlineBackupSize;
    offlinebackOBj_->_offlineCompress_size = _onlineCompress_size;
    offlinebackOBj_->_offlineCurrBackup_size = _onlineBackupSize;
    //save base info
    offlinebackOBj_->_baseChunkNum = _baseChunkNum;
    offlinebackOBj_->_baseDataSize = _baseDataSize;
    //save delta info
    offlinebackOBj_->_deltaChunkNum = _deltaChunkNum;
    offlinebackOBj_->_deltaDataSize = _deltaDataSize;
    offlinebackOBj_->_DeltaSaveSize = _DeltaSaveSize;
    offlinebackOBj_->_lz4SaveSize = _lz4SaveSize;
    //init
    offlinebackOBj_->Init();
    //update
    if(Offline_Flag == true){
        offlinebackOBj_->Easy_update(upOutSGX, cryptoObj_);
    }else{
        offlinebackOBj_->CleanLocal_Index();
    }
    return ;
}



void EcallFreqIndex::OutEntrySFGet(InQueryEntry_t *_inQueryBase, OutQueryEntry_t *_outQueryBase,uint32_t _chunkNum){
   InQueryEntry_t *inQueryEntry = _inQueryBase;
    OutQueryEntry_t *outQueryEntry = _outQueryBase;
    for(size_t i = 0; i < _chunkNum; i++){
        // if the input query is unique, then the output query is also unique
        if(inQueryEntry->dedupFlag == UNIQUE){
            // if the output query is not unique, then the output query is not a delta
            if(outQueryEntry->dedupFlag == UNIQUE){
                outQueryEntry->deltaFlag = NO_DELTA;
                outQueryEntry->offlineFlag = 0;
                memcpy(&outQueryEntry->superfeature, &inQueryEntry->superfeature, 3 * CHUNK_HASH_SIZE);
            }
            // increment the output query entry
            outQueryEntry++;
        }
        // increment the input query entry
        inQueryEntry++;
    }
    return;

}


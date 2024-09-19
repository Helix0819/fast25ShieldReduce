/**
 * @file ecallFreqTwoIndex.h
 * @author Ruilin Wu(202222080631@std.uestc.edu.cn)
 * @brief define the interface of freq two-path index
 * @version 0.1
 * @date 2023-01-15
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef ECALL_MEGA_INDEX_H
#define ECALL_MEGA_INDEX_H


#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <random>
#include <cstddef>
#include "enclaveBase.h"
#include "ecallCMSketch.h"
#include "ecallEntryHeap.h"
#include "ecallinContainercache.h"
#include <sgx_thread.h>


extern "C"
{
#include "config.h"
#include "xdelta3.h"
}


#define SEALED_FREQ_INDEX "freq-index"
#define SEALED_SKETCH "cm-sketch"



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

extern vector<Param> param_list;
extern pthread_mutex_t mutex2;
extern pthread_t test_pthread[4];

class EcallMeGA : public EnclaveBase {
    private:
        string myName_ = "SecureMeGA";

        // the top-k threshold
        size_t topThreshold_;

        // the pointer to the cm-sketch inside the enclave
        EcallCMSketch* cmSketch_;

        // the width of the sketch
        size_t sketchWidth_ = 256 * 1024;
        size_t sketchDepth_ = 4;

        // the deduplication index
        EcallEntryHeap* insideDedupIndex_;
        InContainercache* InContainercache_;

        uint64_t insideDedupChunkNum_ = 0;
        uint64_t insideDedupDataSize_ = 0;

        set<string> basecontainer_set;

        uint8_t *temp_iv;
        uint8_t *temp_chunkbuffer;
        uint8_t *tmp_buffer;
        uint8_t *basechunkbuffer;


        /**
         * @brief update the inside-enclave with only freq
         * 
         * @param ChunkFp the chunk fp
         * @param currentFreq the current frequency
         */
        void UpdateInsideIndexFreq(const string& chunkFp, uint32_t currentFreq);

        /**
         * @brief check whether add this chunk to the heap
         * 
         * @param chunkFreq the chunk freq
         */
        bool CheckIfAddToHeap(uint32_t chunkFreq);

        /**
         * @brief Add the information of this chunk to the heap
         * 
         * @param chunkFreq the chunk freq
         * @param chunkAddr the chunk address
         * @param chunkFp the chunk fp
         */
        void AddChunkToHeap(uint32_t chunkFreq, RecipeEntry_t* chunkAddr, const string& chunkFp);

        /**
         * @brief persist the deduplication index into the disk
         * 
         * @return true success
         * @return false fail
         */
        bool PersistDedupIndex();

        /**
         * @brief read the hook index from sealed data
         * 
         * @return true success
         * @return false fail
         */
        bool LoadDedupIndex();
    public:

        /**
         * @brief Construct a new Ecall Secure MeGA Index object
         * 
         */
        EcallMeGA();

        /**
         * @brief Destroy the Ecall Secure MeGA Index object
         * 
         */
        ~EcallMeGA();

        /**
         * @brief process one batch
         * 
         * @param buffer the recv chunk buffer
         * @param upOutSGX the pointer to the enclave-related var 
         */
        void ProcessOneBatch(SendMsgBuffer_t* recvChunkBuf,
            UpOutSGX_t* upOutSGX);

        /**
         * @brief process the tailed batch when received the end of the recipe flag
         * 
         * @param upOutSGX the pointer to enclave-related var
         */
        void ProcessTailBatch(UpOutSGX_t* upOutSGX);

        /**
         * @brief calculating superfeatures for chunk
         * 
         * @param ptr the pointer of chunk content 
         * @param mdCtx 
         * @param SF the buffer of superfeature
         * @param cryptoObj_ 
         */
        void getSF(unsigned char *ptr, EVP_MD_CTX *mdCtx, uint8_t *SF, EcallCrypto *cryptoObj_);

        /**
         * @brief do delta compression
         * 
         * @param in target chunk buffer 
         * @param in_size target chunk size
         * @param ref base chunk buffer
         * @param ref_size base chunk size
         * @param res_size delta chunk size
         * @return delta chunk buffer
         */
        uint8_t* xd3_encode(const uint8_t *in, size_t in_size, const uint8_t *ref, size_t ref_size, size_t *res_size);

        /**
         * @brief do delta decompression
         * 
         * @param in delta chunk buffer 
         * @param in_size delta chunk size
         * @param ref base chunk buffer
         * @param ref_size base chunk size
         * @param res_size target chunk size
         * @return target chunk buffer
         */
        uint8_t* xd3_decode(const uint8_t *in, size_t in_size, const uint8_t *ref, size_t ref_size, size_t *res_size);

        /**
         * @brief do delta compression
         * 
         * @param inQueryEntry the pointer to inqueryentry
         * @param outQueryEntry the pointer to outqueryentry
         * @param upOutSGX the pointer to enclave-related var
         * @param currentOffset base chunk size
         * @param recvBuffer target chunk size
         * @param deltachunk_size
         * @return target chunk buffer
         */
        uint8_t* ProcessDeltachunk(InQueryEntry_t *inQueryEntry, OutQueryEntry_t *outQueryEntry, UpOutSGX_t *upOutSGX,size_t currentOffset,uint8_t* recvBuffer,size_t *deltachunk_size);

        /**
         * @brief offline phase
         * 
         * @param recvChunkBuf the recv chunk buffer
         * @param upOutSGX the structure to store the enclave related variable
         */
        void ProcessOffline(SendMsgBuffer_t *recvChunkBuf, UpOutSGX_t *upOutSGX);

        /**
         * @brief calculating superfeatures for chunk
         * 
         * @param ptr the pointer of chunk content 
         * @param mdCtx 
         * @param SF the buffer of superfeature
         * @param cryptoObj_ 
         * @param chunkSize 
         */
        static void getSF2(unsigned char *ptr, EVP_MD_CTX *mdCtx, uint8_t *SF, EcallCrypto *cryptoObj_,int chunkSize);

        /**
         * @brief single query for the existence of basechunk
         * 
         * @param _inQueryEntry the pointer of inqueryentry 
         * @param _outQueryEntry the pointer of outqueryentry 
         * @param _upOutSGX the pointer to enclave-related var
         * @param Batch_ContainerIDset the set of basechunk container
         */
        void MeGALoad(InQueryEntry_t *_inQueryEntry, OutQueryEntry_t *_outQueryEntry, UpOutSGX_t *_upOutSGX,unordered_map<string,int> &MeGA_ContainerIDmap);

        /**
         * @brief batch query for the existence of basechunks
         * 
         * @param _inQueryBase the pointer of inqueryentry 
         * @param _outQueryBase the pointer of outqueryentry 
         * @param _upOutSGX the pointer to enclave-related var
         * @param _chunkNum the num of chunk
         */
        bool MeGAChecker(InQueryEntry_t *_inQueryBase, OutQueryEntry_t *_outQueryBase, UpOutSGX_t *_upOutSGX,uint32_t _chunkNum,unordered_map<string,int> &MeGA_ContainerIDmap);

        /**
         * @brief batch processing of delta chunks
         * 
         * @param _inQueryEntry the pointer to inqueryentry 
         * @param _outQueryEntry the pointer to outqueryentry 
         * @param _upOutSGX the pointer to enclave-related var
         * @param _batch_map the map of pair(oldbasechunk, newbasechunk) in this batch
         * @param _batch_out_times number of container loads in this batch
         */
        void MeGAdeltaTure(InQueryEntry_t *_inQueryEntry, OutQueryEntry_t *_outQueryEntry, UpOutSGX_t *_upOutSGX,int &_batch_out_times,unordered_map<string,int> &MeGA_ContainerIDmap);

        static uint32_t* mt_n_get(uint32_t seed, int n);

        /**
         * @brief copy superfeature from inqueryentry to outqueryentry
         * 
         * @param _inQueryBase the pointer to inqueryentry
         * @param _outQueryBase the pointer to outqueryentry
         * @param _chunkNum
         */
        void OutEntrySFGet(InQueryEntry_t *_inQueryBase, OutQueryEntry_t *_outQueryBase,uint32_t _chunkNum);

        static void* GetSF_thread_func_f(void* arg);


};

#endif
/**
* Copyright 2022 The XSCE Authors. All rights reserved.
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* 
*     http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
/**
 * @file pir.cpp
 * @author Created by wumingzi. 2022:05:11,Wednesday,23:09:57.
 * @brief 
 * @version 
 * @date 2022-05-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "pir.h"
#include "toolkits/util/include/xlog.h"
#include "common/pub/include/util.h"
#include "PSI/include/psi.h"

namespace xscePirAlg
{
    using namespace std;
    using namespace oc;
    using namespace util;
    using namespace xsce_ose;

    int64_t pir2PartyAlgTerminalBatch(OptAlg *optAlg)
    {
        int64_t rlt = -1;
        std::string datafn = "";
        int64_t dataCol = 0;
        int64_t headLine = 0;
        std::string rltfn = "";
        bool isServer = false;
        uint32_t hashBitLen = 128;
        uint32_t hashByteLen = 16;
        uint32_t maxStrLen = 128;
        uint64_t maxShowCnt = 200;

        TimeUtils t1, t2, t3;

        //for data reading
        std::vector<std::string> psiStr;
        std::vector<std::string> dataRowVec;
        std::vector<std::vector<std::string>> strMtx;

        LOG_INFO("pir2PartyAlgTerminalBatch pir top level ...");

        if (nullptr == optAlg)
        {
            LOG_ERROR("pir2PartyAlgTerminal input optAlg is null");
            return rlt;
        }
        datafn = optAlg->dataFn;
        dataCol = optAlg->col;
        headLine = optAlg->headLine;
        rltfn = optAlg->rltFn;

        if (0 == optAlg->role)
            isServer = true;

        AlgStatus *status = (AlgStatus *)optAlg->statusPtr;
        if (nullptr == status)
        {
            LOG_ERROR("pir2PartyAlgTerminal input status is null");
            return rlt;
        }

        status->algName = "terminalPir";

        //1. read data string
        LOG_INFO("terminal pir alg. datafn=" << datafn << ",col=" << dataCol << ",headline=" << headLine);

        getStrMtxFromCsvFile(datafn, strMtx);

        int fileRow = strMtx.size();
        int fileCol = 0;
        if (fileRow > 0)
        {
            LOG_INFO("read file,row=" << strMtx.size() << ",col=" << strMtx.at(0).size());
            fileCol = strMtx.at(0).size();
        }
        else
        {
            LOG_ERROR("terminal pir alg. input data file rows =" << fileRow << " is error. ");
            return rlt;
        }

        if (dataCol < 0 || dataCol >= fileCol)
        {
            LOG_ERROR("terminal pir alg. input data col=" << dataCol << " is error. ");
            return rlt;
        }

        if (headLine < 0 || headLine >= fileRow)
        {
            LOG_ERROR("terminal pir alg. input headline =" << headLine << " is error. ");
            return rlt;
        }

        //for both client & server to get the col column data as index data(psi input)
        getStrColFromMtx(psiStr, strMtx, dataCol, headLine);
        LOG_INFO("terminal pir alg. psi str vector size=" << psiStr.size() << " . ");

        if (isServer)
        {
            maxStrLen = getRowStrVecFromCsvFile(datafn, dataRowVec, headLine);
            int64_t dataRow = dataRowVec.size();
            LOG_INFO("terminal pir alg. data row vec size=" << dataRow << ",maxStrLen=" << maxStrLen);
        }

        //for future use, need to keep data in memory, no need to read data from file each time.

        //convert string to hashBuf
        hashByteLen = hashBitLen / (8 * (sizeof(unsigned char)));
        LOG_INFO("terminal pir alg. data hash byte len=" << hashByteLen);

        //2. convert data to hashBuf
        uint32_t *hashBuf = nullptr;
        uint64_t realElementNum = 0;
        int64_t idxCnt = psiStr.size();

        std::vector<uint64_t> seedVec;
        uint64_t seed = optAlg->commonSeed;
        uint64_t seed2 = optAlg->inertalSeed;
        seedVec.push_back(seed);
        seedVec.push_back(seed2);

        std::vector<int64_t> indexId(idxCnt);

        initSortIndex(indexId, idxCnt);
        realElementNum = convertStrVec2Md5Index(psiStr, hashBuf, indexId);

        if (realElementNum > 1)
        {
            int showCnt = realElementNum > maxShowCnt ? maxShowCnt : realElementNum;
            showHexValue(hashBuf, 4, showCnt);
        }

        //3. split data to bucket pool according specified rule

        int secu_key = optAlg->secuK;
        int pool_size = 1;
        int pool_num = optAlg->dataLen;

        if (optAlg->simdLen > 1)
        {
            pool_size = optAlg->simdLen;
        }
        LOG_INFO("secuKey=" << secu_key << ",pool_num=" << pool_num);

        //here begin to split id_data to bucket pool.
        PirDataInfo data_info;

        data_info.secu_key = secu_key;
        data_info.role = optAlg->role;

        data_info.id_hash_buf = hashBuf;
        data_info.id_hash_byte_len = hashByteLen;
        data_info.id_num = realElementNum;

        data_info.original_index_id = &indexId;

        data_info.bucket_pool_size = pool_size;
        data_info.bucket_pool_num = pool_num;
        data_info.index_char_pos = optAlg->intMul;

        data_info.original_id_str = &psiStr;
        if (isServer)
        {
            data_info.original_data_str = &dataRowVec;

            if (maxStrLen <= optAlg->dataRowLen)
            {
                data_info.max_str_len = maxStrLen;
                LOG_INFO("set max_str_len to maxStrLen val=" << data_info.max_str_len);
            }
            else
            {
                data_info.max_str_len = optAlg->dataRowLen;
                LOG_INFO("set max_str_len to opt val=" << data_info.max_str_len);
            }
        }

        splitIdBufBucket(&data_info);

        pool_num = data_info.bucket_pool_num;
        LOG_INFO("pool_num=" << pool_num);

        //here to run pir alg in each pool
        std::vector<std::vector<std::string>> pir_rlt(pool_num);
        std::vector<std::string> total_pir_rlt;

        data_info.pir_rlt = &pir_rlt;
        //here need to use multithread to speed up performance
        std::vector<int64_t> matched_pool_index;
        int64_t matched_pool_num = 0;
        for (int64_t i = 0; i < pool_num; i++)
        {
            auto cur_pool_vol = data_info.bucket_pool_vol.at(i);
            bool zero_vol = checkLocalRmtNumNonZero(optAlg, cur_pool_vol, i);
            showBlk(3, 3);
            LOG_INFO("pir pool  [" << i << "] check remote id_num is valid or not.");
            if (zero_vol)
            {
                // pir2PartyAlgTerminalPool(optAlg, &data_info, i);
                matched_pool_index.push_back(i);
                matched_pool_num++;
            }
            else
            {
                LOG_INFO("pool[" << i << "] is empty,no need to run");
            }
        }

        LOG_INFO("matched pool_num=" << matched_pool_num << ",matched_pool_index=" << matched_pool_index.size());
        // here to use multithread  .Modified by wumingzi/wumingzi. 2022:06:22,Wednesday,22:48:18.
        int max_thread_num = 16;
        int thread_num = optAlg->thdNum;
        int cpu_base = optAlg->cpuNum;
        int thd_loop = 0;

        if (thread_num < 1 || thread_num > max_thread_num)
        {
            thread_num = 1;
            LOG_INFO("set thread number thread_num=" << thread_num);
        }

        std::vector<std::thread> algTask(thread_num);
        std::vector<OptAlg> optVec(thread_num);

        //copy optAlg for each thread
        for (int64_t i = 0; i < thread_num; i++)
        {
            copyThdAlgOpt(&optVec[i], optAlg, i);

            optVec[i].cpuNum = i + cpu_base;
            optVec[i].portNum = 1;
            optVec[i].thdNum = 1;
            optVec[i].thdIdex = i;
        }
        thd_loop = (matched_pool_num + thread_num - 1) / thread_num;
        LOG_INFO("thd_loop=" << thd_loop << ",thread_num=" << thread_num << ",cpu_base=" << cpu_base);

        int query_pool_base = 0;
        int cur_thd_num = thread_num;
        for (int64_t i = 0; i < thd_loop; i++)
        {
            LOG_INFO("query_pool_base=" << query_pool_base << ",loop=" << i);
            if (query_pool_base + cur_thd_num > matched_pool_num)
            {
                cur_thd_num = matched_pool_num - query_pool_base;
                LOG_INFO("last query pool loop=" << i << ", cur_thd_num=" << cur_thd_num);
            }
            LOG_INFO("query_pool_base=" << query_pool_base << ",loop=" << i << ",cur_thd_num=" << cur_thd_num);

            for (int64_t j = 0; j < cur_thd_num; j++)
            {
                int cur_thd_idx = query_pool_base + j;
                int cur_pool_idx = matched_pool_index.at(cur_thd_idx);
                LOG_INFO("start  pir thread loop " << i << ",thdIdx=" << cur_thd_idx << ",cur_pool_idx=" << cur_pool_idx);
                optVec.at(j).thdIdex = cur_thd_idx;
                try
                {
                    algTask[j] = std::thread(pir2PartyAlgTerminalPool, &optVec[j], &data_info, cur_pool_idx);
                }
                catch (const std::system_error &e)
                {
                    std::cerr << "Error in creating new thread at  pir alg!\n"
                              << e.what() << std::endl;
                    continue;
                }
            }

            for (int64_t j = 0; j < cur_thd_num; j++)
            {
                try
                {
                    algTask[j].join();
                }
                catch (const std::system_error &e)
                {
                    std::cerr << "Error in joining thread at pir alg!\n"
                              << e.what() << std::endl;
                    continue;
                }
            }

            //wait for thread over.
            uint64_t thd_Ok_Num = 0;
            std::vector<int64_t> thd_Ok_Vec(cur_thd_num, 0);
            for (;;)
            {
                for (int64_t j = 0; j < cur_thd_num; j++)
                {
                    if (thd_Ok_Vec[j] > 0)
                    {
                        continue;
                    }
                    else
                    {
                        if (optVec.at(j).thdOver)
                        {
                            thd_Ok_Vec[j] = 1;
                            thd_Ok_Num++;
                            LOG_INFO("total thd ok=" << thd_Ok_Num << ",current thdIdx=" << std::dec << i << " is over. ");
                        }
                    }
                }

                if ((uint64_t)cur_thd_num == thd_Ok_Num)
                {
                    LOG_INFO("all thread is over.");
                    optAlg->thdOver = true;
                    break;
                }
                SleepMsec(10);
            }

            query_pool_base += cur_thd_num;
        }

        //   .Modification over by wumingzi/wumingzi. 2022:06:22,Wednesday,22:48:26.

        //. save pri alg result.
        std::string fn = optAlg->rltFn;
        savePirRlt2File(&pir_rlt, fn);

        return rlt;
    }

    int64_t pir2PartyAlgTerminalPool(OptAlg *optAlg, PirDataInfo *data_info, int pool_num)
    {
        int64_t rlt = -1;
        optAlg->thdOver = false;
        if (nullptr == optAlg)
        {
            LOG_ERROR("pir2PartyAlgTerminalPool input optAlg is null");
            optAlg->thdOver = true;
            return rlt;
        }

        if (nullptr == data_info)
        {
            LOG_ERROR("pir2PartyAlgTerminalPool input data_info is null");
            optAlg->thdOver = true;
            return rlt;
        }

        auto max_pool_num = data_info->bucket_pool_num;
        if (pool_num < 0 || pool_num >= max_pool_num)
        {
            LOG_ERROR("pir2PartyAlgTerminalPool input pool_num is invalid=" << pool_num);
            optAlg->thdOver = true;
            return rlt;
        }

        bool is_server = (0 == optAlg->role) ? true : false;

        std::string msg;
        auto check_rlt = checkPirDataInfo(data_info, msg);
        if (!check_rlt)
        {
            LOG_ERROR("pir2PartyAlgTerminalPool input pirDataInfo is invalid.");
            LOG_ERROR(msg);
            optAlg->thdOver = true;
            return rlt;
        }

        LOG_INFO("begin to run pir alg in bucket pool[" << pool_num << "].");

        //here to extract data info.

        uint8_t *data_buf = nullptr;
        int64_t pool_volume = data_info->bucket_pool_vol.at(pool_num);
        uint64_t hash_byte_length = data_info->id_hash_byte_len;
        bool mock_data_flag = true; //true means simulate one fault id&data for pir alg when this pool is empty.

        uint8_t idle_id[hash_byte_length];
        std::vector<std::string> *data_row_vec = nullptr;
        std::vector<std::string> idle_data_row;

        //here to prepare idle data for empty pool.
        if (is_server)
        {
            idle_data_row.resize(hash_byte_length);
            for (uint64_t i = 0; i < hash_byte_length; i++)
            {
                idle_data_row.at(i) = '0' + data_info->idle_data_row;
                idle_id[i] = data_info->idle_id_str_srv;
            }
        }
        else
        {
            for (uint64_t i = 0; i < hash_byte_length; i++)
            {
                idle_id[i] = data_info->idle_id_str_cli;
            }
        }

        if (pool_volume > 0)
        {
            data_buf = (uint8_t *)data_info->bucket_pool_buf.at(pool_num);
            if (is_server)
            {
                data_row_vec = &data_info->bucket_pool_data_row.at(pool_num);
            }
        }
        else
        {
            LOG_INFO("this pool is empty.");
            if (mock_data_flag)
            {
                data_buf = idle_id;
                pool_volume = 1;
                if (is_server)
                {
                    data_row_vec = &idle_data_row;
                }
            }
        }

        //maybe need to coordinate bucket index to speedup alg performance.

        //here srv&cli both have id_str/data row. begin to call basic pir alg.
        bool use_bucket_flag = true;
        std::vector<std::string> &pir_result = data_info->pir_rlt->at(pool_num);
        PirAlgInfo alg;
        alg.result = &pir_result;
        alg.id_hash_buf = data_buf;
        alg.id_num = pool_volume;
        alg.hash_byte_len = hash_byte_length;
        alg.pool_index = pool_num;
        alg.max_str_len = data_info->max_str_len;
        alg.secu_key = optAlg->secuK;

        if (is_server)
        {
            alg.data_row = data_row_vec;
        }

        //here to split bucket in current pool and remove unused bucket from data_buf.
        use_bucket_flag = false;
        if (use_bucket_flag)
        {
            LOG_INFO("use pir pool split bucket ");
            pirPoolSplitBucket(optAlg, &alg);
        }

        auto pir_rlt = pir2PartyAlgTerminalBasic(optAlg, &alg);

        LOG_INFO("pir rlt=" << pir_rlt);
        for (uint64_t i = 0; i < pir_result.size(); i++)
        {
            LOG_INFO("pool[" << pool_num << "]. rlt[" << i << "]=" << pir_result.at(i));
        }

        optAlg->thdOver = true;
        return rlt;
    }

    int64_t pir2PartyAlgTerminalBasic(OptAlg *optAlg, PirAlgInfo *alg_info)
    {
        int64_t rlt = -1;
        int64_t tmp_rlt = -1;
        if (nullptr == optAlg)
        {
            LOG_ERROR("pir2PartyAlgTerminalBasic input optAlg is null");
            return rlt;
        }

        if (nullptr == alg_info)
        {
            LOG_ERROR("pir2PartyAlgTerminalBasic input alg_info is null");
            return rlt;
        }

        bool is_server = (0 == optAlg->role) ? true : false;
        bool mock_idle_pir = false; //if true, even psi rlt is null,client still run ot with server.

        int64_t id_num = alg_info->id_num;
        uint8_t *hash_buf = alg_info->id_hash_buf;
        std::vector<std::string> *data_row = alg_info->data_row; //hold the data string for srv
        std::vector<std::string> *result = alg_info->result;     //hold the id string for both srv&cli
        uint64_t rmt_id_num = 0;

        if ((nullptr == hash_buf || nullptr == result))
        {
            LOG_ERROR("hash_buf/result is null.exit");
            return rlt;
        }

        if (is_server && nullptr == data_row)
        {
            LOG_ERROR("data_row is null.exit");
            return rlt;
        }

        //for future.here to check rmt id_num is whether or not.

        //for encode/decode buf use.
        uint64_t *encode_buf = nullptr; // save encrypted data string.
        uint64_t *key_buf = nullptr;    // save aes key. generated by encStrVec2Buf internally.
        uint64_t *decode_buf = nullptr; // client save server's ciphertext data.
        uint64_t *cipher_buf = nullptr;

        std::string ch_name = "pirTerminalBatch";
        uint64_t encode_len = alg_info->max_str_len;
        uint64_t str_encode_len = encode_len;
        uint64_t str_decode_len = 0;
        uint64_t total_encode_buf_len = 0;
        uint64_t aes_len = 0;
        uint64_t str_num = 0;
        std::vector<int64_t> index_id(id_num);
        std::vector<uint64_t> seedVec;
        uint64_t seed = optAlg->commonSeed;
        seedVec.push_back(seed);
        uint64_t max_show_cnt = optAlg->showCnt;

        //for psi use
        uint64_t match_id_num = 0;
        std::vector<uint64_t> psi_result;
        std::vector<uint64_t> srv_resutl_index;

        initSortIndex(index_id, id_num);
        LOG_INFO("pir2PartyAlgTerminalBasic. id_num=" << id_num);

        //. run psi to get server data index which meets client query id
        // for now,only use oprf psi alg  .Modified by wumingzi/wumingzi. 2022:04:21,Thursday,21:35:12.
        std::vector<block> oprf_values;
        {
            LOG_INFO("use oprf psi for pir alg.");
            LOG_INFO("begin to show hash buf. before psi...");
            showBlk(1, 2);
            xscePsiAlg::hashbufPsiAlgClient((uint64_t *)hash_buf, id_num, psi_result, optAlg, srv_resutl_index, oprf_values);
            showBlk(2, 2);
            LOG_INFO("begin to show hash buf. after psi...");
            showBlk(1, 2);
        }
        //   .Modification over by wumingzi/wumingzi. 2022:04:21,Thursday,21:35:25.

        LOG_INFO("pir alg .  psi match rlt=" << psi_result.size() << ",srv rlt vec size=" << srv_resutl_index.size());

        match_id_num = psi_result.size();
        if (!is_server)
        {
            if (psi_result.size() != srv_resutl_index.size())
            {
                LOG_INFO("hash buf psi alg result is error. psiRlt.size()=" << psi_result.size() << ",srvRLtVec.size()=" << srv_resutl_index.size());
                return rlt;
            }

            for (uint64_t i = 0; i < match_id_num && i < max_show_cnt; i++)
            {
                LOG_INFO(i << ":idx=" << psi_result[i] << ",srv idx=" << srv_resutl_index.at(i));
            }
        }

        //server to encode data row to cipher data.
        std::vector<uint64_t> key_buf_vec;
        for (auto b : oprf_values)
        {
            auto tmp = b.as<uint64_t>();
            key_buf_vec.push_back(tmp[0]);
            key_buf_vec.push_back(tmp[1]);
        }
        key_buf = key_buf_vec.data();
        if (is_server)
        {
            encStr2BufIndex(data_row, encode_len, seedVec, &encode_buf, key_buf, &index_id);

            str_num = data_row->size();
            aes_len = (str_encode_len + 15) / 16;
            str_encode_len = 2 * aes_len; // single string enc length in uint64 size.
            total_encode_buf_len = str_encode_len * str_num;

            rmt_id_num = getUint32FromRmt(optAlg, str_encode_len, ch_name);
            LOG_INFO("srv get rmt id_num=" << rmt_id_num);
            for (uint64_t i = 0; i < str_num; i++)
            {
                LOG_INFO("pir srv data[" << i << "]=" << data_row->at(i));
            }
        }
        else
        {
            //here client to get str_decode_len from server.
            str_decode_len = getUint32FromRmt(optAlg, id_num, ch_name);
            LOG_INFO("cli get  str_decode_len=" << str_decode_len);
        }
        LOG_INFO("show opt 0:str_num=" << str_num << ",id_num=" << id_num << ",aes_len=" << aes_len << ",str_decode_len=" << str_decode_len << ",total_encode_buf_len=" << total_encode_buf_len);

        //. server send cipher data to client
        if (is_server)
        {
            srvSendBuf(optAlg, encode_buf, total_encode_buf_len);
        }
        else
        {
            cliRcvBuf(optAlg, &decode_buf, &total_encode_buf_len);
        }

        LOG_INFO("show opt 1:str_num=" << str_num << ",id_num=" << id_num << ",max_show_cnt=" << max_show_cnt << ",str_decode_len=" << str_decode_len << ",total_encode_buf_len=" << total_encode_buf_len);

        if (is_server)
        {
            showHexValue((uint32_t *)encode_buf, 4, max_show_cnt);
        }
        else
        {
            showHexValue((uint32_t *)decode_buf, 4, max_show_cnt);
        }

        //. client run ot to get aes key from server
        if (mock_idle_pir)
        {
            if (0 == match_id_num)
            {
                LOG_INFO("need to mock pir query with server.");
                match_id_num = 1;
                psi_result.push_back(0);
                srv_resutl_index.push_back(0);
            }
        }

        //. server decode cipher data to plaintext data by aes key.
        if (!is_server)
        {
            tmp_rlt = pirAesDecode((uint8_t *)decode_buf, (uint8_t *)key_buf,
                                   str_decode_len, psi_result, srv_resutl_index, result);

            if (tmp_rlt < 0)
            {
                LOG_INFO("pirAesDecode error");
            }
            else
            {
                for (uint64_t i = 0; i < result->size() && i < max_show_cnt; i++)
                {
                    LOG_INFO("pir alg result[" << i << "]=" << result->at(i));
                }
            }
        }

        //free memory
        freeUInt64Vec(encode_buf);
        freeUInt64Vec(decode_buf);
        freeUInt64Vec(cipher_buf);

        return rlt;
    }

    bool checkLocalRmtNumNonZero(OptAlg *optAlg, int64_t num, int pool)
    {
        bool result = true;
        std::string ch_name = "check_local_rmt_num" + std::to_string(pool);
        int send_num = num > 0 ? 1 : 0;
        auto rmt_num = getUint32FromRmt(optAlg, send_num, ch_name);

        if (0 == send_num || 0 == rmt_num)
        {
            result = false;
        }

        return result;
    }

    //split pool id_hash_buf to multiple buckets according to security key(which is usually 100 or 1000,000)
    int64_t pirPoolSplitBucket(OptAlg *optAlg, PirAlgInfo *alg_info)
    {
        int64_t rlt = -1;
        if (nullptr == optAlg)
        {
            LOG_ERROR("pirPoolSplitBucket input optAlg is null");
            return rlt;
        }

        if (nullptr == alg_info)
        {
            LOG_ERROR("pirPoolSplitBucket input alg_info is null");
            return rlt;
        }
        bool is_server = (0 == optAlg->role) ? true : false;

        //first read parameters from alg_info
        uint8_t *id_hash_buf = alg_info->id_hash_buf;    //id hash number buf.
        int32_t hash_byte_len = alg_info->hash_byte_len; //the byte length of each hash vaule.
        int64_t id_num = alg_info->id_num;               //the number of id string.
        int secu_key = alg_info->secu_key;
        int64_t bucket_num = 1;
        uint8_t *bucket_range_buf = nullptr;
        uint64_t *bucket_range_buf_cli = nullptr;
        int64_t tmp_rlt = 0;

        std::vector<std::string> *data_row = alg_info->data_row;
        if (is_server && nullptr == data_row)
        {
            LOG_ERROR("pirPoolSplitBucket input data_row is null");
            return rlt;
        }

        //sort id_hash_buf
        std::vector<uint64_t> id_hash_index(id_num);
        initSortIndex(id_hash_index, id_num);
        alg_info->id_hash_index = &id_hash_index;

        int id_hash_len_in_uin32t = hash_byte_len / sizeof(uint32_t);
        int shwo_cnt = 100;
        int min_bucket_size = 2;

        int max_show_cnt = id_num > shwo_cnt ? shwo_cnt : id_num;
        LOG_INFO("id id_hash_len_in_uin32t len=" << id_hash_len_in_uin32t << ",hash_byte_len=" << hash_byte_len);

        LOG_INFO("show original hash value");
        showHexValue((uint32_t *)id_hash_buf, 4, max_show_cnt);

        mergeSortUIntBuf((uint32_t *)id_hash_buf, id_hash_len_in_uin32t, 0, id_num - 1, id_hash_index);

        LOG_INFO("show sort hash value");
        showHexValue((uint32_t *)id_hash_buf, 4, max_show_cnt);

        for (int64_t i = 0; i < max_show_cnt; i++)
        {
            LOG_INFO("index[" << i << "]=" << id_hash_index.at(i));
        }
        showBlk(2, 1);

        //then split to bucket
        if (secu_key < min_bucket_size)
        {
            secu_key = min_bucket_size;
            alg_info->secu_key = secu_key;
            LOG_INFO("secu_key to min_bucket_size=" << secu_key);
        }

        if (is_server)
        {
            tmp_rlt = pirBucketRange(alg_info, &bucket_range_buf, &bucket_num);
            LOG_INFO("pirBucketRange result = " << std::dec << tmp_rlt);
            alg_info->total_bucket_num = bucket_num;

            if (tmp_rlt < 0)
            {
                LOG_ERROR("pirBucketRange rlt error.");
                if (bucket_range_buf)
                    free(bucket_range_buf);
                return rlt;
            }
        }

        //here  client to recieve server bucket range.
        std::string ch_name = "pirAlgBucket" + optAlg->taskId;
        if (is_server)
        {
            uint64_t bucket_send_num = bucket_num * hash_byte_len / sizeof(uint64_t);
            LOG_INFO("server send   bucket num buf size=" << bucket_send_num);
            srvSendBuf(optAlg, (uint64_t *)bucket_range_buf, bucket_send_num);
        }
        else
        {
            uint64_t rcv_len = 0;
            cliRcvBuf(optAlg, &bucket_range_buf_cli, &rcv_len);
            bucket_num = rcv_len / (hash_byte_len / sizeof(uint64_t));
            alg_info->bucket_range_buf = bucket_range_buf_cli;
            alg_info->total_bucket_num = bucket_num;
            LOG_INFO("client get server bucket num=" << bucket_num);
        }
        LOG_INFO("srv cli exchange bucket range over.");
        //server & client coordinates which buckets to run pir alg

        tmp_rlt = pirBucketFilter(optAlg, alg_info);
        if (tmp_rlt < 1)
        {
            LOG_ERROR("client find usable pir bucket num = " << tmp_rlt);
            return rlt;
        }

        LOG_INFO("pirBucketFilter over.");

        //update alg_info data_buf & data_row.
        tmp_rlt = pirBucketDataUpdate(optAlg, alg_info);
        LOG_INFO("pirBucketDataUpdate over.");

        return rlt;
    }

    // remove dismatched bucket&data   .Modified by wumingzi/wumingzi. 2022:06:21,Tuesday,17:22:32.
    int64_t pirBucketDataUpdate(OptAlg *optAlg, PirAlgInfo *alg_info)
    {
        int64_t rlt = -1;
        if (nullptr == alg_info || nullptr == optAlg)
        {
            LOG_ERROR("pirBucketFilter input is null");
            return rlt;
        }

        bool is_server = (0 == optAlg->role) ? true : false;

        int64_t bucket = 0;
        uint8_t *id_hash_buf = alg_info->id_hash_buf;                   //id hash number buf.
        std::vector<std::string> *data_row = alg_info->data_row;        //id hash number buf.
        std::vector<uint64_t> *id_hash_index = alg_info->id_hash_index; //id hash number buf.
        std::vector<int64_t> &id_bucket_index = alg_info->id_bucket_index;
        std::vector<uint64_t> &id_range_index = alg_info->id_range_index; //holds id range in bucket after updating data

        int32_t hash_byte_len = alg_info->hash_byte_len; //the byte length of each hash vaule.
        int64_t id_num = alg_info->id_num;               //the number of id string.
        int secu_key = alg_info->secu_key;
        bucket = alg_info->total_bucket_num;

        LOG_INFO("pirBucketDataUpdate bucket=" << bucket);
        std::vector<int64_t> &matched_bucket = alg_info->matched_bucket;

        LOG_INFO("matched_bucket =" << matched_bucket.size());
        LOG_INFO("id_bucket_index =" << id_bucket_index.size());
        LOG_INFO("id_num =" << id_num);

        if (nullptr == id_hash_index)
        {
            LOG_ERROR("id_hash_index is error.");
            return rlt;
        }

        int64_t data_base = 0;
        int64_t id_buf_offset = 0;
        int64_t new_id_buf_offset = 0;
        int64_t data_buf_offset = 0;
        int64_t original_data_index = 0;
        int64_t bucket_index = 0;
        int64_t copy_bucket_len = 0;
        uint64_t copy_data_cnt = 0;

        if (!is_server)
        {
            uint8_t *tmp_hash_buf = nullptr;
            int64_t new_data_vol = id_num;
            std::vector<int64_t> new_id_bucket_index;
            tmp_hash_buf = allocateUCharVec(new_data_vol * hash_byte_len);
            if (nullptr == tmp_hash_buf)
            {
                LOG_ERROR("tmp_hash_buf allocate error.");
                return rlt;
            }

            id_range_index.resize(0);
            new_id_bucket_index.resize(0);
            std::vector<int64_t> new_id_range();

            uint64_t bucket_index = 0;
            for (int64_t i = 0; i < id_num; i++)
            {
                bucket_index = id_bucket_index.at(i);
                if (i >= 0 && i < bucket)
                {
                    //here current id is matched in bucket.
                    new_id_bucket_index.push_back(bucket_index);
                    if (0 == id_range_index.size())
                    { //first bucket index,no need to check duplicate
                        id_range_index.push_back(bucket_index);
                    }
                    else
                    {
                        if (bucket_index != id_range_index.front())
                            id_range_index.push_back(bucket_index);
                    }

                    //current id is contained in bucket
                    //copy id_hash
                    id_buf_offset = hash_byte_len * i;
                    new_id_buf_offset = copy_data_cnt * hash_byte_len;
                    copyBufData(id_hash_buf + id_buf_offset, tmp_hash_buf + new_id_buf_offset, hash_byte_len);
                    //here increase copy_data_cnt
                    copy_data_cnt++;
                }
            }

            //here copy id_range_index to id_bucket_index
            auto new_id_num = new_id_bucket_index.size();
            if (new_id_num != copy_data_cnt)
            {
                LOG_ERROR("new_id_num error =" << new_id_num << ",copy_data_cnt=" << copy_data_cnt);
                return rlt;
            }

            id_bucket_index.resize(new_id_num);
            for (uint64_t i = 0; i < new_id_num; i++)
            {
                id_bucket_index.at(i) = new_id_bucket_index.at(i);
            }

            //here calculate id_range_index
            id_range_index.resize(0);

            LOG_INFO("new_id_num=" << new_id_num << ",copy_data_cnt=" << copy_data_cnt);
            free(alg_info->id_hash_buf);
            alg_info->id_hash_buf = tmp_hash_buf;
            alg_info->id_num = copy_data_cnt;

            LOG_INFO("cli data update over.");
        }

        if (is_server)
        {
            uint8_t *tmp_hash_buf = nullptr;
            auto len = matched_bucket.size();
            int64_t new_data_vol = len * secu_key;
            tmp_hash_buf = allocateUCharVec(new_data_vol * hash_byte_len);

            if (nullptr == tmp_hash_buf)
            {
                LOG_ERROR("tmp_hash_buf allocate error.");
                return rlt;
            }
            if (nullptr == data_row)
            {
                LOG_ERROR("data_row is error.");
                return rlt;
            }

            std::vector<std::string> new_data_row(new_data_vol);

            for (size_t i = 0; i < len; i++)
            {
                bucket_index = matched_bucket.at(i);
                if (bucket_index < 0 || bucket_index >= bucket)
                {
                    LOG_INFO("invliad bucket index=" << bucket_index);
                    continue;
                }
                data_base = secu_key * bucket_index;

                //copy each iterm in the bucket.
                //need to consider the last bucket
                copy_bucket_len = secu_key;
                if (data_base + secu_key >= id_num)
                {
                    copy_bucket_len = id_num - data_base;
                    LOG_INFO("last bucket in bucket copy. set copy_bucke_len=" << copy_bucket_len);
                }
                for (int64_t j = 0; j < copy_bucket_len; j++)
                {
                    //copy data row
                    data_buf_offset = data_base + j;
                    original_data_index = id_hash_index->at(data_buf_offset);
                    new_data_row.at(copy_data_cnt) = data_row->at(original_data_index);

                    LOG_INFO("matched bucket[" << i << "], data_buf_offset=" << data_buf_offset << ",original_data_index=" << original_data_index);
                    //copy id_hash
                    id_buf_offset = hash_byte_len * (data_base + j);
                    new_id_buf_offset = copy_data_cnt * hash_byte_len;
                    copyBufData(id_hash_buf + id_buf_offset, tmp_hash_buf + new_id_buf_offset, hash_byte_len);
                    //here increase copy_data_cnt
                    copy_data_cnt++;
                }
            }

            //here copy tmporary data to alg_info
            LOG_INFO("copy_data_cnt=" << copy_data_cnt << ",new data row=" << new_data_row.size());
            free(alg_info->id_hash_buf);
            alg_info->id_hash_buf = tmp_hash_buf;
            data_row->resize(copy_data_cnt);
            for (uint64_t i = 0; i < copy_data_cnt; i++)
            {
                data_row->at(i) = new_data_row.at(i);
            }
            alg_info->id_num = copy_data_cnt;

            //reinitialize id_hash_index.
            id_hash_index->resize(copy_data_cnt);
            for (uint64_t i = 0; i < copy_data_cnt; i++)
            {
                id_hash_index->at(i) = i;
            }

            LOG_INFO("srv data update over.");
        }

        rlt = copy_data_cnt;
        return rlt;
    }

    // remove bucket number which contains no query id  .Modified by wumingzi/wumingzi. 2022:06:21,Tuesday,15:26:18.
    int64_t pirBucketFilter(OptAlg *optAlg, PirAlgInfo *alg_info)
    {
        int64_t rlt = -1;
        if (nullptr == alg_info || nullptr == optAlg)
        {
            LOG_ERROR("pirBucketFilter input is null");
            return rlt;
        }

        bool is_server = (0 == optAlg->role) ? true : false;
        uint64_t *bucket_range_buf_cli = nullptr;
        uint64_t show_cnt = optAlg->showCnt;
        uint64_t max_show_cnt = show_cnt;

        uint8_t *id_hash_buf = alg_info->id_hash_buf;    //id hash number buf.
        int32_t hash_byte_len = alg_info->hash_byte_len; //the byte length of each hash vaule.
        uint64_t id_num = alg_info->id_num;               //the number of id string.
        uint64_t bucket = alg_info->total_bucket_num;

        LOG_INFO("pirBucketFilter bucket=" << bucket);

        if (!is_server)
        {
            bucket_range_buf_cli = alg_info->bucket_range_buf;

            if (nullptr == bucket_range_buf_cli || bucket < 1)
            {
                LOG_ERROR("cli pirBucketFilter bucket error");
            }
        }

        if (!is_server)
        {
            alg_info->id_bucket_index.resize(id_num);
            for (uint64_t i = 0; i < id_num; i++)
                alg_info->id_bucket_index.at(i) = -1;

            uint8_t *s1 = nullptr;
            uint64_t *s2 = nullptr;
            int64_t offset_id = 0;
            int64_t offset_bucket = 0;
            uint64_t bucket_cnt = 0;
            int64_t matched_id_num = 0;
            LOG_INFO("id_num=" << id_num << ",bucket num=" << bucket);

            for (uint64_t i = 0; i < id_num && bucket_cnt < bucket;)
            {
                offset_id = i * hash_byte_len;
                offset_bucket = bucket_cnt * hash_byte_len / sizeof(uint64_t);

                s1 = id_hash_buf + offset_id;
                s2 = bucket_range_buf_cli + offset_bucket;

                bool cmp = getUInt32Buf((uint32_t *)s2, (uint32_t *)s1, hash_byte_len / sizeof(uint32_t));

                if (cmp) //current id is in bucket[bucket_cnt]
                {
                    alg_info->id_bucket_index.at(i++) = bucket_cnt;
                    matched_id_num++;
                    continue;
                }
                else //current id exceeds bucket[bucket_cnt]
                {
                    bucket_cnt++;
                    continue;
                }
            }
            LOG_INFO("cli matched id number=" << matched_id_num);
        }

        uint64_t *cli_bucket_index_buf = nullptr;
        uint64_t matched_bucket_num = 0;

        if (!is_server)
        {
            std::vector<int64_t> vec;
            vec.push_back(-1); //-1 will always be sent to server even matched_bucket_num = 0;
            for (uint64_t i = 0; i < id_num; i++)
            {
                auto index = alg_info->id_bucket_index.at(i);
                if (index > vec.front())
                {
                    vec.push_back(index);
                }
            }
            auto len = vec.size();
            cli_bucket_index_buf = allocateUInt64Vec(len);
            if (nullptr == cli_bucket_index_buf)
            {
                LOG_ERROR("cli_bucket_index_buf allocate mem error");
                return rlt;
            }

            for (uint64_t i = 0; i < len; i++)
            {
                cli_bucket_index_buf[i] = vec.at(i);
            }

            matched_bucket_num = vec.size() - 1;
        }

        //now client send matched bucket index to server.
        if (!is_server)
        {
            cliSendBuf(optAlg, (uint64_t *)cli_bucket_index_buf, matched_bucket_num + 1);
        }
        else
        {
            srvRcvBuf(optAlg, &cli_bucket_index_buf, &matched_bucket_num);
            matched_bucket_num--;
            LOG_INFO("srv get matched bucket num=" << matched_bucket_num);

            max_show_cnt = matched_bucket_num > show_cnt ? show_cnt : matched_bucket_num;

            for (uint64_t i = 0; i < max_show_cnt; i++)
            {
                auto index = cli_bucket_index_buf[i + 1];
                if (index >= 0 && index < bucket)
                {
                    alg_info->matched_bucket.push_back(index);
                }
                if (i < max_show_cnt)
                    LOG_INFO("srv matched bucket[" << i << "]=" << std::dec << index);
            }
        }

        rlt = matched_bucket_num;
        return rlt;
    }

    int64_t pirBucketRange(PirAlgInfo *alg_info, uint8_t **range_buf, int64_t *bucket_num)
    {
        int64_t rlt = -1;

        if (nullptr == alg_info || nullptr == range_buf || nullptr == bucket_num)
        {
            LOG_ERROR("pirBucketRange input is null");
            return rlt;
        }

        int64_t bucket = 0;
        uint8_t *id_hash_buf = alg_info->id_hash_buf;    //id hash number buf.
        int32_t hash_byte_len = alg_info->hash_byte_len; //the byte length of each hash vaule.
        int64_t id_num = alg_info->id_num;               //the number of id string.
        int secu_key = alg_info->secu_key;
        bool garble_hash_val = false;

        uint8_t *bucket_range_buf = nullptr;
        //calculate bucket number
        if (id_num < 1 || secu_key < 1)
        {
            LOG_ERROR("pirBucketRange input secu_key or id_num is too samll");
            return rlt;
        }

        if (alg_info->garble_hash_value > 0)
        {
            garble_hash_val = true;
            LOG_INFO("garble hash value. set to the average of two hash value in range buound");
        }

        bucket = (id_num + secu_key - 1) / secu_key;
        LOG_INFO(std::dec << "pirBucketRange bucket=" << bucket << ",id_num=" << id_num << ",secu_key=" << secu_key);
        bucket_range_buf = (uint8_t *)allocateUInt32Vec(bucket * hash_byte_len / sizeof(uint32_t));
        uint64_t *dst = nullptr;
        if (nullptr == bucket_range_buf)
        {
            LOG_ERROR("pirBucketRange allocate buffer error");
            return rlt;
        }

        uint8_t *s1 = nullptr;
        uint8_t *s2 = nullptr;
        int64_t offset = 0;
        for (int64_t i = 0; i < bucket - 1; i++)
        {
            offset = (i + 1) * secu_key - 1;
            offset = offset * hash_byte_len;
            s1 = id_hash_buf + offset;
            s2 = s1 + hash_byte_len;

            dst = (uint64_t *)(bucket_range_buf + i * hash_byte_len);

            if (garble_hash_val)
            {
                auto tmp_rlt = avgUInt128BufSimple((uint32_t *)s2, (uint32_t *)s1, (uint32_t *)dst);
                if (!tmp_rlt)
                {
                    LOG_ERROR("pirBucketRange errot at bucket=" << i);
                    LOG_ERROR("s1:");
                    showHexValue((uint32_t *)s1, 4, 1);
                    LOG_ERROR("s2:");
                    showHexValue((uint32_t *)s2, 4, 1);
                    return rlt;
                }
            }
            else //no need to calculate avg of two hash val.
            {
                uint8_t *dst_u8 = bucket_range_buf + i * hash_byte_len;
                for (int64_t j = 0; j < hash_byte_len; j++)
                {
                    dst_u8[j] = s2[j];
                }
            }
        }

        LOG_INFO("set last buf range to all 1 here.");
        //the last bucket range nees special consideration.
        dst = (uint64_t *)(bucket_range_buf + (bucket - 1) * hash_byte_len);
        dst[0] = 0xffffffff;
        dst[1] = 0xffffffff;

        LOG_INFO("save bucket range result  here.");
        //save result
        *bucket_num = bucket;

        *range_buf = bucket_range_buf;

        LOG_INFO("check bucket range result  here.");
        //here to check
        for (int64_t i = 0; i < bucket - 1; i++)
        {
            offset = (i + 1) * secu_key - 1;
            offset = offset * hash_byte_len;
            s1 = id_hash_buf + offset;
            s2 = s1 + hash_byte_len;

            dst = (uint64_t *)(bucket_range_buf + (i)*hash_byte_len);

            bool cmp1 = letUInt32Buf((uint32_t *)s1, (uint32_t *)dst, 4);
            bool cmp2 = letUInt32Buf((uint32_t *)dst, (uint32_t *)s2, 4);
            if (!cmp1 || !cmp2)
            {
                LOG_ERROR("bucket range error at " << i);
                LOG_ERROR("s1:");
                showHexValue((uint32_t *)s1, 4, 1);
                LOG_ERROR("s2:");
                showHexValue((uint32_t *)s2, 4, 1);
                LOG_ERROR("range:");
                showHexValue((uint32_t *)dst, 4, 1);
                return rlt;
            }
        }

        rlt = bucket;

        return rlt;
    }

    int64_t splitIdBufBucket(PirDataInfo *data_info)
    {
        int64_t rlt = -1;
        if (nullptr == data_info)
        {
            LOG_ERROR("splitIdBufBucket input is null");
            return rlt;
        }

        //the maximum id number in pir alg.
        uint64_t max_id_num = 1000 * 1000 * 1000;
        uint64_t max_pool_size = 1000 * 1000 * 10;
        uint64_t min_pool_size = 1;
        uint64_t max_pool_num = 256 * 256;
        bool bucket_pool_num_mode = true; //true means use bucket number

        uint8_t *data_buf = (uint8_t *)data_info->id_hash_buf;
        if (nullptr == data_buf)
        {
            LOG_ERROR("splitIdBufBucket input is null");
            return rlt;
        }

        uint64_t hash_byte_length = data_info->id_hash_byte_len;

        if (hash_byte_length < 1 || hash_byte_length % 8 != 0)
        {
            LOG_ERROR("splitIdBufBucket input hash_byte_length is invalid=" << hash_byte_length);
            return rlt;
        }

        uint64_t index_char_pos = (uint64_t)data_info->index_char_pos;
        if (index_char_pos < 0 || index_char_pos >= hash_byte_length)
        {
            LOG_ERROR("splitIdBufBucket input index_char_pos is invalid， set to 0" << index_char_pos);
            // return rlt;
            index_char_pos = 0;
            data_info->index_char_pos = 0;
        }

        uint64_t id_num = data_info->id_num;
        if (id_num < 1 || id_num > max_id_num)
        {
            LOG_ERROR("splitIdBufBucket input id_num is invalid=" << id_num);
            return rlt;
        }

        uint64_t bucket_pool_num = data_info->bucket_pool_num;
        if (bucket_pool_num < 1 || bucket_pool_num > max_pool_num)
        {
            bucket_pool_num = 1;
            data_info->bucket_pool_num = 1;
        }

        LOG_INFO("splitIdBufBucket bucket_pool_num=" << bucket_pool_num << ",index_char_pos=" << index_char_pos);

        std::vector<int64_t> *original_index_id = data_info->original_index_id;
        if (nullptr == original_index_id)
        {
            LOG_ERROR("splitIdBufBucket input original_index_id is null");
            return rlt;
        }

        uint64_t bucket_pool_size = data_info->bucket_pool_size;
        if (bucket_pool_size < min_pool_size)
        {
            bucket_pool_size = min_pool_size;
        }

        if (bucket_pool_size > max_pool_size)
        {
            bucket_pool_size = max_pool_size;
        }
        data_info->bucket_pool_size = bucket_pool_size;

        int secu_key = data_info->secu_key;
        if (secu_key < 100 || secu_key >= 1000 * 1000)
        {
            secu_key = 100;
            // data_info->secu_key = 100;
        }

        //here bein to split bucket pool. for now, bucket_pool_size is not used.
        int64_t pool_num = data_info->bucket_pool_num;
        LOG_INFO("split bucket pool, id num=" << std::dec << id_num << ",pool_num=" << pool_num << ",bucke_pool_size=" << bucket_pool_size);
        LOG_INFO("split bucket pool, index_char_pos=" << index_char_pos << ",secuKey=" << secu_key);

        if (bucket_pool_num_mode)
        {
            LOG_INFO("split data to bucket pool in fixed pool number mode.");
            auto split_rlt = splitIdBufBucketByPoolNum(data_info);
            rlt = split_rlt;
            showBlk(2, 2);
            showPirDataInfo(data_info);

            LOG_INFO("check pirDataInfo validity");
            std::string msg;
            if (!checkPirDataInfo(data_info, msg))
            {
                LOG_INFO(msg);
            }
            else
            {
                LOG_INFO("pirDataInfo is ok.");
            }
        }
        else //in pool size mode
        {
            LOG_INFO("split data to bucket pool in fixed pool volume mode.");
        }

        return rlt;
    }

    // split buf to pool  .Modified by wumingzi/wumingzi. 2022:06:15,Wednesday,23:52:34.
    int64_t splitIdBufBucketByPoolNum(PirDataInfo *data_info)
    {
        int64_t rlt = -1;
        if (nullptr == data_info)
        {
            LOG_ERROR("splitIdBufBucketByPoolNum input is null");
            return rlt;
        }

        bool is_server = (0 == data_info->role) ? true : false;

        uint8_t *data_buf = (uint8_t *)data_info->id_hash_buf;
        uint64_t hash_byte_length = data_info->id_hash_byte_len;
        int index_char_pos = data_info->index_char_pos;
        int index_char_len = 1;
        uint64_t id_num = data_info->id_num;

        //for now, only support 256 bucket pool at most.
        uint64_t bucket_pool_sum = 256;

        uint64_t bucket_pool_num = data_info->bucket_pool_num;

        if (256 % bucket_pool_num != 0)
        {
            bucket_pool_num = bucket_pool_sum;
            data_info->bucket_pool_num = bucket_pool_sum;
            LOG_INFO("splitIdBufBucketByPoolNum, set bucket_pool_num to default vale =" << bucket_pool_sum);
        }

        LOG_INFO("splitIdBufBucketByPoolNum,id_num=" << id_num << ",bucket_pool_sum=" << bucket_pool_sum << ",pool num=" << bucket_pool_num);
        LOG_INFO("splitIdBufBucketByPoolNum,index_char_pos=" << index_char_pos << ",bucket_pool_sum=" << bucket_pool_sum << ",pool num=" << bucket_pool_num);

        int64_t base = 0;
        int64_t offset = 0;
        uint8_t curVal = 0;
        int pool_index = 0;
        std::vector<int64_t> pool_volume(bucket_pool_num, 0);
        std::vector<int64_t> id_index_pool(id_num);
        data_info->bucket_pool_vol.resize(bucket_pool_num, 0);
        data_info->bucket_pool_index_id.resize(id_num, 0);

        for (uint64_t i = 0; i < id_num; i++)
        {
            offset = base + index_char_pos;

            //only valid for 1 byte index_len,if index_char_len is larger than 1,should use the following
            //when index_char_len=2 curVal = data_buf[offset] + 256*data_buf[offset+1]
            //when index_char_len=3 curVal = data_buf[offset] + 256*data_buf[offset+1]+256*256*data_buf[offset+2]
            curVal = data_buf[offset];
            pool_index = GET_POOL_INDEX(curVal, bucket_pool_sum, bucket_pool_num);
            pool_volume[pool_index]++;
            id_index_pool[i] = pool_index;
            data_info->bucket_pool_index_id.at(i) = pool_index;

            base += hash_byte_length;
        }

        //allocate buffer size for each pool
        std::vector<uint32_t *> &bucket_pool_buf = data_info->bucket_pool_buf;
        std::vector<std::vector<std::string>> &bucket_pool_data_row = data_info->bucket_pool_data_row;
        std::vector<std::vector<std::string>> &id_str = data_info->id_str;
        std::vector<int64_t> *original_index_id = data_info->original_index_id;

        std::vector<std::string> *original_id_str = data_info->original_id_str;
        std::vector<std::string> *original_data_str = data_info->original_data_str;

        if (nullptr == original_index_id)
        {
            LOG_ERROR("original_index_id is null. exit");
            return rlt;
        }
        if (nullptr == original_id_str)
        {
            LOG_ERROR("original_id_str is null. exit");
            return rlt;
        }
        if (is_server && nullptr == original_data_str)
        {
            LOG_ERROR("original_data_str is null. exit");
            return rlt;
        }

        bucket_pool_buf.resize(bucket_pool_num);
        for (uint64_t i = 0; i < bucket_pool_num; i++)
        {
            auto buf_len = pool_volume[i];

            data_info->bucket_pool_vol.at(i) = buf_len;

            int64_t mem_len = buf_len * hash_byte_length / sizeof(uint32_t);
            LOG_INFO("bucket_pool_buf[" << i << "] size=" << buf_len << ",mem_len=" << mem_len);
            uint32_t *cur_pool_buf = nullptr;
            if (buf_len > 0)
            {
                cur_pool_buf = allocateUInt32Vec(mem_len);
                if (nullptr == cur_pool_buf)
                {
                    LOG_ERROR("splitIdBufBucketByPoolNum allocate buf error at pool=" << i << ".");
                    return rlt;
                }
            }

            bucket_pool_buf.at(i) = cur_pool_buf;
        }

        //copy data buf to each pool buffer
        std::vector<int64_t> pool_buf_offset(bucket_pool_num, 0);
        if (is_server)
        {
            bucket_pool_data_row.resize(bucket_pool_num);
        }
        id_str.resize(bucket_pool_num);

        int64_t buf_offset = 0;
        int original_index = 0;
        for (uint64_t i = 0; i < id_num; i++)
        {
            pool_index = id_index_pool[i];
            buf_offset = pool_buf_offset[pool_index];
            uint8_t *buf_dst = (uint8_t *)&bucket_pool_buf.at(pool_index)[buf_offset];
            uint8_t *buf_src = (uint8_t *)&data_buf[i * hash_byte_length];

            showHexValue((uint32_t *)buf_src, 4, 1);
            copyBufWithIndex(buf_dst, buf_src, hash_byte_length, index_char_pos, index_char_len);
            showHexValue((uint32_t *)buf_dst, 4, 1);

            //move data row to pool
            original_index = original_index_id->at(i);
            id_str.at(pool_index).push_back(original_id_str->at(original_index));
            if (is_server)
            {
                bucket_pool_data_row.at(pool_index).push_back(original_data_str->at(original_index));
            }
            //move offset to next hash vaule
            pool_buf_offset[pool_index] += hash_byte_length / sizeof(uint32_t);
        }

        return rlt;
    }

    bool checkPirDataInfo(PirDataInfo *data_info, std::string &msg)
    {
        int error_num = 0;
        std::stringstream log;
        if (nullptr == data_info)
        {
            log << "data_info input is null" << std::endl;
            error_num++;
        }

        bool is_server = false;
        if (0 == data_info->role)
            is_server = true;
        //first check buf pointer.
        if (nullptr == data_info->original_index_id)
        {
            log << "data_info original_index_id is null" << std::endl;
            error_num++;
        }

        if (nullptr == data_info->original_id_str)
        {
            log << "data_info original_id_str is null" << std::endl;
            error_num++;
        }

        if (is_server) //server
        {
            if (nullptr == data_info->original_data_str)
            {
                log << "data_info(pir server) original_data_str is null" << std::endl;
                error_num++;
            }
        }

        //check id_sum
        auto id_num = data_info->id_num;
        int64_t len = data_info->original_index_id->size();
        if (id_num != len)
        {
            log << "data_info  original_index_id size=" << len << " is not the same as id_num=" << id_num << std::endl;
            error_num++;
        }
        len = data_info->original_id_str->size();
        if (id_num != len)
        {
            log << "data_info  original_id_str size=" << len << " is not the same as id_num=" << id_num << std::endl;
            error_num++;
        }

        if (is_server) //server
        {
            len = data_info->original_data_str->size();
            if (id_num != len)
            {
                log << "data_info  original_data_str size=" << len << " is not the same as id_num=" << id_num << std::endl;
                error_num++;
            }
        }

        len = data_info->bucket_pool_index_id.size();
        if (id_num != len)
        {
            log << "data_info  bucket_pool_index_id size=" << len << " is not the same as id_num=" << id_num << std::endl;
            error_num++;
        }

        //check vector size is whether the same as pool number
        int64_t pool_num = data_info->bucket_pool_num;
        len = data_info->id_str.size();

        if (pool_num != len)
        {
            log << "data_info id_str vec size=" << len << ", is not the same as pool num=" << pool_num << std::endl;
            error_num++;
        }

        len = data_info->bucket_pool_buf.size();
        if (pool_num != len)
        {
            error_num++;
            log << "data_info bucket_pool_buf vec size=" << len << ", is not the same as pool num=" << pool_num << std::endl;
        }

        len = data_info->bucket_pool_vol.size();
        if (pool_num != len)
        {
            error_num++;
            log << "data_info bucket_pool_vol vec size=" << len << ", is not the same as pool num=" << pool_num << std::endl;
        }

        if (is_server)
        {
            len = data_info->bucket_pool_data_row.size();
            if (pool_num != len)
            {
                error_num++;
                log << "data_info bucket_pool_data_row vec size=" << len << ", is not the same as pool num=" << pool_num << std::endl;
            }
        }

        //check hash_byte length.
        len = data_info->id_hash_byte_len;
        if (len < 16)
        {
            error_num++;
            log << "data_info id_hash_byte_len id_hash_byte_len=" << len << " is invalid." << std::endl;
        }

        if (error_num > 0)
        {
            msg = log.str();
            return false;
        }
        else
        {
            return true;
        }
    }

    void showPirDataInfo(PirDataInfo *data_info, int show_cnt)
    {
        std::stringstream log;
        if (nullptr == data_info)
            return;

        std::vector<uint32_t *> &bucket_pool_buf = data_info->bucket_pool_buf;
        int len = bucket_pool_buf.size();
        
        for (int64_t i = 0; i < len; i++)
        {
            auto pool_volume = data_info->id_str.at(i).size();
            if (pool_volume < 1)
            {
                log << "bucket pool[" << i << "] is empty." << std::endl;
                continue;
            }

            uint32_t *buf = data_info->bucket_pool_buf.at(i);
            auto max_show_cnt = pool_volume;
            if (show_cnt > 0 && show_cnt < (int)pool_volume)
            {
                max_show_cnt = show_cnt;
            }

            log << "pool[" << i << "] volume=" << pool_volume << std::endl;

            showHexValue(buf, data_info->id_hash_byte_len / sizeof(uint32_t), max_show_cnt);

            for (uint64_t j = 0; j < pool_volume && j < max_show_cnt; j++)
            {
                log << "id=" << data_info->id_str.at(i).at(j) << std::endl;
                if (0 == data_info->role)
                {
                    log << "id=" << data_info->bucket_pool_data_row.at(i).at(j) << std::endl;
                }
            }
        }

        LOG_INFO("\n" << log.str());
    }

  
    //aes decode function.   .Modified by wumingzi. 2022:06:17,Friday,23:58:26.
    //cipher_buf: ciphder data (entire cipher data from pir server side, only decodes part of them whose aes key is available)
    //key_buf: key data
    //msg_aes_len:  data length in uint64_t type. 128 bits means msg_aes_len = 2
    // cli_rlt: indicates which cipher data's aes key is stored in key_buf.
    //srv_rlt:  indicates which cipher data.
    //str_rlt:  decode cipher data to string saved here.
    int64_t pirAesDecode(uint8_t *cipher_buf, uint8_t *key_buf,
                         int msg_aes_len,
                         const std::vector<uint64_t> &cli_rlt,
                         const std::vector<uint64_t> &srv_rlt,
                         std::vector<std::string> *str_rlt)
    {
        int64_t rlt = -1;
        int key_len = 2; //128 bit aes. key_len = 2 means 2 uint64_t size.

        if (nullptr == cipher_buf)
        {
            LOG_ERROR("pirAesDecode cipher_buf is null");
            return rlt;
        }
        if (nullptr == key_buf)
        {
            LOG_ERROR("pirAesDecode key_buf is null");
            return rlt;
        }
        if (nullptr == str_rlt)
        {
            LOG_ERROR("pirAesDecode str_rlt is null");
            return rlt;
        }

        if (srv_rlt.size() != cli_rlt.size())
        {
            LOG_ERROR("the size srv_rlt is not same as cli_rlt");
            return rlt;
        }

        auto len = srv_rlt.size();
        if (len < 1)
        {
            LOG_ERROR("pirAesDecode srv_rlt is null");
            return rlt;
        }

        if (msg_aes_len < key_len || 0 != msg_aes_len % key_len)
        {
            LOG_ERROR("pirAesDecode msg_aes_len is invalid=" << msg_aes_len);
            return rlt;
        }

        str_rlt->resize(len);
        uint64_t *dst = nullptr;
        uint64_t *src = (uint64_t *)cipher_buf;
        uint64_t *key = (uint64_t *)key_buf;
        uint32_t max_show_cnt = 3;

        dst = (uint64_t *)calloc(len * msg_aes_len, sizeof(uint64_t));
        if (nullptr == dst)
        {
            LOG_ERROR("dst buf allocation error.exit");
            return rlt;
        }

        int offset = 0;
        LOG_INFO("pir aes decoe. len=" << len);
        for (uint32_t i = 0; i < len; i++)
        {
            int64_t index_srv = srv_rlt.at(i);
            int64_t index_cli = cli_rlt.at(i);
            offset = index_srv * msg_aes_len;
            util::aesDecBUf(src + offset, dst + i * msg_aes_len, msg_aes_len, key + index_cli * key_len);

            //for debug
            if (i < max_show_cnt)
            {
                LOG_INFO("index:" << index_cli);
                block b(*(key + index_cli * key_len), *(key + index_cli * key_len + 1));
                LOG_INFO("key:" << b);

                uint8_t *charBuf = (uint8_t *)(dst + i * msg_aes_len);
                std::stringstream ss;
                ss << i << ": dec rlt=";
                for (int64_t j = 0; j < msg_aes_len * 8; j++)
                {
                    ss << (unsigned char)charBuf[j];
                }
                LOG_INFO(ss.str());
                showBlk(2, 1);
                rlt++;
            }
        }

        auto save_rlt = savePirRlt2Vec(srv_rlt, msg_aes_len, (uint64_t *)dst, str_rlt);
        LOG_INFO("save_rlt=" << save_rlt);

        //free  mem
        freeUInt64Vec(dst);

        if (save_rlt < 0)
        {
            LOG_ERROR("savePirRlt2Vec error.");
            return -1;
        }

        return rlt;
    }

    // save pir result to vector  .Modified by wumingzi. 2022:06:18,Saturday,00:37:43.
    int64_t savePirRlt2Vec(const std::vector<uint64_t> &psi_rlt, int64_t msg_aes_len, uint64_t *str_buf, std::vector<std::string> *rlt_str)
    {
        int64_t rlt = -1;

        std::stringstream log;

        if (nullptr == str_buf)
        {
            LOG_ERROR("savePirRlt2Vec error. str_buf  is nullptr");
            return rlt;
        }
        if (nullptr == rlt_str)
        {
            LOG_ERROR("savePirRlt2Vec error. rlt_str  is nullptr");
            return rlt;
        }

        if (msg_aes_len < 1)
        {
            LOG_ERROR("savePirRlt2Vec error. msg_aes_len  error size=" << msg_aes_len);
            return rlt;
        }

        int64_t len = psi_rlt.size();
        rlt_str->resize(0);
        for (int64_t i = 0; i < len; i++)
        {
            uint8_t *charBuf = (uint8_t *)(str_buf + i * msg_aes_len);

            for (int64_t j = 0; j < msg_aes_len * 8; j++)
            {
                log << (unsigned char)charBuf[j];
            }
            log << std::endl;

            rlt_str->push_back(log.str());
            log.str("");
        }

        rlt = rlt_str->size();

        return rlt;
    }

 
    // here to combine multiple pirOpt to one pirOpt  .Modified by wumingzi/wumingzi. 2022:04:23,Saturday,19:15:52.
    //we should copy multiple hash buffer data to one hash buf
    //and so does with encBuf/keyBuf/
    int64_t combinePirOpt(OptAlg *optAlg, std::vector<PirBasicOpt> *pirOptVec, PirBasicOpt *pirOpt, int base, int len)
    {
        int64_t rlt = -1;
        int64_t tmpRLt = 0;

        if (nullptr == optAlg)
        {
            LOG_ERROR("combinePirOpt input optAlg is null");
            return rlt;
        }
        if (nullptr == pirOptVec)
        {
            LOG_ERROR("combinePirOpt input pirOptVec is null");
            return rlt;
        }

        if (nullptr == pirOpt)
        {
            LOG_ERROR("combinePirOpt input pirOptVec is null");
            return rlt;
        }

        int64_t totalPirCnt = pirOptVec->size();

        if (base < 0 || len < 1)
        {
            LOG_ERROR("combinePirOpt input base=" << base << " or len=" << len << " is error");
            return rlt;
        }
        if (base + len > totalPirCnt)
        {
            LOG_ERROR("combinePirOpt input base=" << base << " or len=" << len << " overflows ,totalPirCnt=" << totalPirCnt);
            return rlt;
        }
        
        bool isServer = false;
        if (0 == optAlg->role)
            isServer = true;

        //first calculate total element
        int64_t newElement = 0;
        int64_t startLoc = 0;
        std::vector<int64_t> startVec(len, 0);
        std::vector<uint64_t *> encBufVec(len, nullptr);
        std::vector<uint64_t *> keyBufVec(len, nullptr);
        std::vector<int64_t> eleVec(len, 0);

        //for client only
        std::vector<int64_t> srvBucketLenVec(len, 0);  //to save srv bucket length
        std::vector<int64_t> srvBucketBaseVec(len, 0); //to save srv bucket startLoc in enc/dec buffer.
        int64_t newSrvElement = 0;

        uint32_t *hashBuf = nullptr;    //buf for hash data
        uint64_t *encBufBase = nullptr; // save encrypted data string.
        uint64_t *keyBufBase = nullptr; // save aes key. generated by encStrVec2Buf internally.
        uint64_t strEncLen = 16;        //data string encrypted length in bytes
        uint64_t keyLen = 16;           //aes key lenght in bytes

        uint32_t *hashBufTotal = pirOptVec->at(base).hashBuf;

        for (int64_t i = 0; i < len; i++)
        {
            PirBasicOpt *opt = &pirOptVec->at(base + i);

            newElement += opt->element;

            startLoc = opt->startLoc;
            startVec.at(i) = startLoc;
            eleVec.at(i) = opt->element;
            strEncLen = opt->strEncLen;

            if (!isServer)
            {
                encBufVec.at(i) = opt->encBufBase;
                srvBucketLenVec.at(i) = opt->srvBucketLen;
                srvBucketBaseVec.at(i) = opt->srvBucketBase;
                newSrvElement += opt->srvBucketLen;
            }
            else
            {
                encBufVec.at(i) = opt->encBufBase;
                keyBufVec.at(i) = opt->keyBufBase;

                LOG_INFO("server save srv bucket[" << i << "], start loc=" << startLoc << ",len=" << opt->element << ",strEncLen=" << strEncLen);
            }

            if (0 == i)
            {
                pirOpt->startLoc = opt->startLoc;
                pirOpt->totalElement = opt->totalElement;
                pirOpt->pirRlt = opt->pirRlt;
                pirOpt->pirRltIdx = opt->pirRltIdx;

                pirOpt->idxVec = opt->idxVec;
                pirOpt->dataVec = opt->dataVec;

                pirOpt->num = opt->num;

                pirOpt->keyLen = opt->keyLen;
            }
        }

        int hashUint32Len = 16 / sizeof(uint32_t); //current hash is 16bytes long.
        int keyUint32Len = keyLen / sizeof(uint32_t);
        int keyUint64Len = keyLen / sizeof(uint64_t);
        int strUint32Len = strEncLen * 2;

        LOG_INFO("newElement=" << newElement << ",newSrvElement=" << newSrvElement << ",hashUint32Len=" << hashUint32Len
                  << ",strUint32Len=" << strUint32Len << ",keyUint32Len=" << keyUint32Len << ",strEncLen=" << strEncLen);

        if (newElement < 0)
        {
            LOG_ERROR("combinePirOpt newElement is errorr=" << newElement);
            goto errorRlt;
        }

        //hash buffer copy is the same for both server & client
        hashBuf = allocateUInt32Vec(hashUint32Len * newElement);
        if (nullptr == hashBuf)
        {
            LOG_ERROR("combinePirOpt allocate hash buffer error.");
            goto errorRlt;
        }

        //first copy hashbuf
        tmpRLt = copyPirOptBuf(hashBuf, hashBufTotal, &startVec, &eleVec, hashUint32Len, newElement * hashUint32Len);
        if (tmpRLt < 0)
        {
            LOG_ERROR("copy pir opt hash buf error=" << tmpRLt);
            goto errorRlt;
        }

        //server enc/key buffer copy here
        if (isServer)
        {
            encBufBase = allocateUInt64Vec(newElement * strEncLen);
            keyBufBase = allocateUInt64Vec(newElement * keyUint64Len);
            if (nullptr == encBufBase || nullptr == keyBufBase)
            {
                LOG_ERROR("combinePirOpt allocate encBufBase or keyBufBase buffer error.");
                goto errorRlt;
            }
        }

        if (isServer)
        {
            tmpRLt = copyPirOptEncBuf(encBufBase, &encBufVec, &eleVec, strEncLen, newElement * strEncLen);
            if (tmpRLt < 0)
            {
                LOG_ERROR("copy pir opt enc buf error=" << tmpRLt);
                goto errorRlt;
            }
            tmpRLt = copyPirOptEncBuf(keyBufBase, &keyBufVec, &eleVec, keyUint64Len, newElement * keyUint64Len);
            if (tmpRLt < 0)
            {
                LOG_ERROR("copy pir opt key buf error=" << tmpRLt);
                goto errorRlt;
            }
        }

        //client to copy dec buffer
        if (!isServer)
        {
            encBufBase = allocateUInt64Vec(newSrvElement * strEncLen);

            if (nullptr == encBufBase)
            {
                LOG_ERROR("combinePirOpt allocate encBufBase  buffer error.");
                goto errorRlt;
            }

            tmpRLt = copyPirOptEncBuf(encBufBase, &encBufVec, &srvBucketLenVec, strEncLen, newSrvElement * strEncLen);
            if (tmpRLt < 0)
            {
                LOG_ERROR("copy pir opt enc buf error=" << tmpRLt);
                goto errorRlt;
            }
        }

        pirOpt->startLoc = 0; //0 means it's in the new buffer space

        pirOpt->hashBuf = hashBuf;
        pirOpt->element = newElement;

        pirOpt->keyLen = keyLen;
        pirOpt->strEncLen = strEncLen;
        if (isServer)
        {
            pirOpt->encBufBase = encBufBase;
            pirOpt->keyBufBase = keyBufBase;
        }
        else
        {
            pirOpt->encBufBase = encBufBase;
            pirOpt->srvBucketBase = 0;
            pirOpt->srvBucketLen = newSrvElement;
        }

        //here combine is ok
        LOG_INFO("here copy buffer  over.");
        // begin to use pirOpt to run pir  .Modified by wumingzi/wumingzi. 2022:04:24,Sunday,10:45:08.

        goto goodRlt;

    errorRlt: //here is error result.
        freeUInt32Vec(hashBuf);
        freeUInt64Vec(encBufBase);
        freeUInt64Vec(keyBufBase);
        return rlt;

        //in ok case, memory buffer is freed outside.
    goodRlt:
        LOG_INFO("combine pir opt return good");
        return 0;
    }

    std::string showPirOpt(PirBasicOpt *opt)
    {
        std::string rlt = "";
        if (nullptr == opt)
        {
            return rlt;
        }
        std::stringstream log;
        log << std::dec << "pir opt[" << opt->num << "]:"
            << std::dec << ",startLoc=" << opt->startLoc << "+" << opt->element << std::endl;
        log << std::dec << "pir opt[" << opt->num << "]:"
            << ",hashBuf=" << opt->hashBuf << ": id vec ptr=" << opt->idxVec << std::endl;
        log << std::dec << "pir opt[" << opt->num << "]:"
            << ",srv enc buf=" << opt->encBufBase << ",key buf=" << opt->keyBufBase << std::endl;

        return log.str();
    }

    //copy hash/enc/key buffer data from multiple pirOpt to one
    //srcVec: input, the buffer head address of each bucket
    //lenVec: input, the number of element in each bucket to copy
    //eleLen: input, the lenght in u64int buffer for each element.
    //total:  total copy nubmer in u32int type.
    //dst: output. the buffer to save all bucket data.
    int64_t copyPirOptEncBuf(uint64_t *dst, std::vector<uint64_t *> *srcVec, std::vector<int64_t> *lenVec, int eleLen, int64_t total)
    {
        int64_t rlt = -1;

        if (nullptr == dst || nullptr == srcVec)
        {
            LOG_ERROR("copyPirOptBuf input dst/srcVec is null");
            return rlt;
        }

        if (nullptr == lenVec)
        {
            LOG_ERROR("copyPirOptBuf input lenVec is null");
            return rlt;
        }

        if (eleLen < 1)
        {
            LOG_ERROR("copyPirOptBuf input u32len is error=" << eleLen);
            return rlt;
        }

        int64_t totalCopy = 0;

        int len = srcVec->size();
        int len2 = lenVec->size();
        if (len != len2)
        {
            LOG_ERROR("copyPirOptBuf input srcVec/lenVec size is error=" << len << ",len2=" << len2);
            return rlt;
        }

        int64_t copyLen = 0;
        uint64_t *copyBase = nullptr;

        for (int64_t i = 0; i < len; i++)
        {

            copyLen = lenVec->at(i) * eleLen;
            copyBase = srcVec->at(i);
            if (nullptr == copyBase)
            {
                LOG_ERROR("copyPirOptEncBuf. srcVec buffer at " << i << " is null. exit");
                break;
            }
            for (int64_t j = 0; j < copyLen; j++)
            {
                dst[totalCopy++] = copyBase[j];
            }
        }

        if (totalCopy != total)
        {
            LOG_ERROR("copyPirOptBuf error. totalCopy=" << totalCopy << ",input total=" << total);
        }
        else
        {
            rlt = 0;
        }

        return rlt;
    }

    //copy hash buffer data from multiple pirOpt to one. here src points to the original start addr of all idVec hashBuf,not only current bucket.
    //eleLen: input, the lenght in u32int buffer for each element.
    //total:  total copy nubmer in u32int type.
    int64_t copyPirOptBuf(uint32_t *dst, uint32_t *src, std::vector<int64_t> *startVec, std::vector<int64_t> *lenVec, int eleLen, int64_t total)
    {
        int64_t rlt = -1;

        if (nullptr == dst || nullptr == src)
        {
            LOG_ERROR("copyPirOptBuf input dst/src is null");
            return rlt;
        }

        if (nullptr == startVec || nullptr == lenVec)
        {
            LOG_ERROR("copyPirOptBuf input startVec/lenVec is null");
            return rlt;
        }

        if (eleLen < 1)
        {
            LOG_ERROR("copyPirOptBuf input u32len is error=" << eleLen);
            return rlt;
        }

        int64_t totalCopy = 0;

        int len = startVec->size();
        int len2 = lenVec->size();
        if (len != len2)
        {
            LOG_ERROR("copyPirOptBuf input startVec/lenVec size is error=" << len << ",len2=" << len2);
            return rlt;
        }

        int64_t copyLen = 0;
        int64_t base = 0;
        uint32_t *copyBase = nullptr;

        for (int64_t i = 0; i < len; i++)
        {
            base = startVec->at(i) * eleLen;
            copyLen = lenVec->at(i) * eleLen;
            copyBase = src + base;
            for (int64_t j = 0; j < copyLen; j++)
            {
                dst[totalCopy++] = copyBase[j];
            }
        }

        if (totalCopy != total)
        {
            LOG_ERROR("copyPirOptBuf error. totalCopy=" << totalCopy << ",input total=" << total);
        }
        else
        {
            rlt = 0;
        }

        return rlt;
    }

    int64_t freePirOptBuf(PirBasicOpt *pirOpt)
    {
        int64_t rlt = -1;
        static int64_t freeCnt = 0;

        if (nullptr == pirOpt)
        {
            LOG_ERROR("freePirOptBuf input pirOpt is null");
            return rlt;
        }

        if (pirOpt->encBufBase)
        {
            free(pirOpt->encBufBase);
            LOG_INFO("free pir opt encBufBase buffer time=" << freeCnt);
        }

        if (pirOpt->keyBufBase)
        {
            free(pirOpt->keyBufBase);
            LOG_INFO("free pir opt keyBufBase buffer time=" << freeCnt);
        }

        if (pirOpt->hashBuf)
        {
            free(pirOpt->hashBuf);
            LOG_INFO("free pir opt hashBuf buffer time=" << freeCnt);
        }

        freeCnt++;
        return rlt;
    }

    //   .Modification over by wumingzi/wumingzi. 2022:04:23,Saturday,19:40:28.
    int64_t savePirStrBufRlt(uint64_t *strBuf, int64_t strNum, int64_t decStrEncLen, std::vector<std::string> *rltStr)
    {
        int64_t rlt = -1;
        std::stringstream log;

        if (decStrEncLen < 1)
        {
            LOG_ERROR("savePirRlt error. decStrEncLen  error size=" << decStrEncLen);
            return rlt;
        }

        if (nullptr == rltStr)
        {
            LOG_ERROR("savePirRlt error. rltStr is null=" << rltStr);
            return rlt;
        }

        for (int64_t i = 0; i < strNum; i++)
        {

            uint8_t *charBuf = (uint8_t *)(strBuf + i * decStrEncLen);

            for (int64_t j = 0; j < decStrEncLen * 8; j++)
            {
                log << (unsigned char)charBuf[j];
            }

            rltStr->push_back(log.str());
            log.str("");
        }

        rlt = rltStr->size();

        return rlt;
    }

}

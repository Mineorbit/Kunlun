#ifndef KUNLUN_CWPRF_PSI_HPP_
#define KUNLUN_CWPRF_PSI_HPP_

#include "../../crypto/ec_point.hpp"
#include "../../crypto/hash.hpp"
#include "../../crypto/prg.hpp"
#include "../../crypto/block.hpp"
#include "../../netio/stream_channel.hpp"
#include "../../filter/bloom_filter.hpp"
#include "../../utility/serialization.hpp"


/*
** implement cwPRF-based PSI
** it is insteresting that cwPRF-based mqPMT does not readily admit Bloom filter optimization
** it depends on the interpreation of the encoding

** in any OPRF-based PSI protocol, for correctness it suffices to truncate the output length of F to \lambda+log(n1)+log(n2) 
** Reference [CRYPTO 2019 - PRTY - SpOT Lightweight PSI from Sparse OT Extension]
** !!! Warning: the truncatation optimization only works when the output is random over {0,1}^l since the above analysis is made in this setting
** one should be careful when the output is random over G, which is sparse over {0,1}^l
** in this case: the most prudent manner is to hash the output to {0,1}^l via a CRHF

** when |G| is not too sparse over {0,1}^l for some l, e.g. the case of curve25519, one could loosely deem that the compressed form is random over {0,1}^l
** LSB of g^{ab} (resp. x-coordinate) are indistinguishable from a random bit-string in number-theoreric (resp. EC) group 
** Reference [EUROCRYPT 2009 - Optimal Randomness Extraction from a Diffie-Hellman Element]

** actually, to ensure correctness, the essence is to identify an efficient CRHF from F's output to {0,1}^l
** truncation is argubly the simplest method, in this case the collision resistance stems from pseduorandom randomness
*/

namespace cwPRFPSI{

using Serialization::operator<<; 
using Serialization::operator>>; 

struct PP
{
    size_t statistical_security_parameter;  // default=40 
    size_t computational_security_parameter; // default=128   
    size_t LOG_SENDER_ITEM_NUM; 
    size_t SENDER_ITEM_NUM; 
    size_t LOG_RECEIVER_ITEM_NUM; 
    size_t RECEIVER_ITEM_NUM; 
    size_t TRUNCATE_LEN; // the truncate length of PRF value
};

// seriazlize
std::ofstream &operator<<(std::ofstream &fout, const PP &pp)
{
    fout << pp.statistical_security_parameter; 
    fout << pp.computational_security_parameter; 
    fout << pp.LOG_SENDER_ITEM_NUM;
    fout << pp.SENDER_ITEM_NUM; 
    fout << pp.LOG_RECEIVER_ITEM_NUM;
    fout << pp.RECEIVER_ITEM_NUM; 
    fout << pp.TRUNCATE_LEN; 
    return fout; 
}

// load pp from file
std::ifstream &operator>>(std::ifstream &fin, PP &pp)
{
    fin >> pp.statistical_security_parameter; 
    fin >> pp.computational_security_parameter; 
    fin >> pp.LOG_SENDER_ITEM_NUM;
    fin >> pp.SENDER_ITEM_NUM; 
    fin >> pp.LOG_RECEIVER_ITEM_NUM;
    fin >> pp.RECEIVER_ITEM_NUM; 
    fin >> pp.TRUNCATE_LEN; 

    return fin; 
}

PP Setup(size_t computational_security_parameter, 
         size_t statistical_security_parameter, 
         size_t LOG_SENDER_ITEM_NUM, 
         size_t LOG_RECEIVER_ITEM_NUM)
{
    PP pp; 
    pp.statistical_security_parameter = statistical_security_parameter;
    pp.computational_security_parameter = computational_security_parameter;  
    pp.LOG_SENDER_ITEM_NUM = LOG_SENDER_ITEM_NUM; 
    pp.SENDER_ITEM_NUM = size_t(pow(2, pp.LOG_SENDER_ITEM_NUM)); 
    pp.LOG_RECEIVER_ITEM_NUM = LOG_RECEIVER_ITEM_NUM; 
    pp.RECEIVER_ITEM_NUM = size_t(pow(2, pp.LOG_RECEIVER_ITEM_NUM)); 
    /*
    ** for PTSY SpOT-Light: Lightweight Private Set Intersection from Sparse OT Extension
    ** page 10 for this parameter choice
    */
    pp.TRUNCATE_LEN = (pp.statistical_security_parameter+pp.LOG_SENDER_ITEM_NUM+pp.LOG_RECEIVER_ITEM_NUM+7)/8; 
    
    return pp; 
}

void SavePP(PP &pp, std::string pp_filename)
{
    std::ofstream fout; 
    fout.open(pp_filename, std::ios::binary); 
    if(!fout){
        std::cerr << pp_filename << " open error" << std::endl;
        exit(1); 
    }
    fout << pp; 
    fout.close(); 
}

void FetchPP(PP &pp, std::string pp_filename)
{
    std::ifstream fin; 
    fin.open(pp_filename, std::ios::binary); 
    if(!fin){
        std::cerr << pp_filename << " open error" << std::endl;
        exit(1); 
    }
    fin >> pp; 
    fin.close(); 
}

void Send(NetIO &io, PP &pp, std::vector<block> &vec_Y)
{
    if(vec_Y.size() != pp.SENDER_ITEM_NUM){
        std::cerr << "input size of vec_Y does not match public parameters" << std::endl;
        exit(1);  // EXIT_FAILURE  
    }

    PrintSplitLine('-'); 
    auto start_time = std::chrono::steady_clock::now(); 

    uint8_t k1[32];
    PRG::Seed seed = PRG::SetSeed(fixed_seed, 0); // initialize PRG
    GenRandomBytes(seed, k1, 32);  // pick a key k1

    std::vector<EC25519Point> vec_Hash_Y(pp.SENDER_ITEM_NUM);
    std::vector<EC25519Point> vec_Fk1_Y(pp.SENDER_ITEM_NUM);

    #pragma omp parallel for num_threads(NUMBER_OF_THREADS)
    for(auto i = 0; i < pp.SENDER_ITEM_NUM; i++){
        Hash::BlockToBytes(vec_Y[i], vec_Hash_Y[i].px, 32); 
        x25519_scalar_mulx(vec_Fk1_Y[i].px, k1, vec_Hash_Y[i].px); 
    }

    io.SendEC25519Points(vec_Fk1_Y.data(), pp.SENDER_ITEM_NUM); 
    
    std::cout <<"cwPRF-based PSI [step 1]: Sender ===> F_k1(y_i) ===> Receiver";
    std::cout << " [" << 32*pp.SENDER_ITEM_NUM/(1024*1024) << " MB]" << std::endl;

    std::vector<EC25519Point> vec_Fk2_X(pp.RECEIVER_ITEM_NUM); 
    io.ReceiveEC25519Points(vec_Fk2_X.data(), pp.RECEIVER_ITEM_NUM);

    std::vector<EC25519Point> vec_Fk1k2_X(pp.RECEIVER_ITEM_NUM); 
    #pragma omp parallel for num_threads(NUMBER_OF_THREADS)
    for(auto i = 0; i < pp.RECEIVER_ITEM_NUM; i++){ 
        x25519_scalar_mulx(vec_Fk1k2_X[i].px, k1, vec_Fk2_X[i].px); // (H(x_i)^k2)^k1
    }

    std::vector<std::string> vec_TRUNCATE_Fk1k2_X(pp.RECEIVER_ITEM_NUM);
    for(auto i = 0; i < pp.RECEIVER_ITEM_NUM; i++){ 
        vec_TRUNCATE_Fk1k2_X[i] = 
            std::string(&vec_Fk1k2_X[i].px[0], &vec_Fk1k2_X[i].px[0]+pp.TRUNCATE_LEN); 
    }

    io.SendStringVector(vec_TRUNCATE_Fk1k2_X, pp.TRUNCATE_LEN); 
    std::cout <<"cwPRF-based PSI [step 3]: Sender ===> Truncate(F_k1k2(x_i)) ===> Receiver";
    std::cout << " [" << pp.TRUNCATE_LEN*pp.RECEIVER_ITEM_NUM/(1024*1024) << " MB]" << std::endl;

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "cwPRF-based PSI: Sender side takes time = " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;
    
    PrintSplitLine('-'); 
}

std::vector<block> Receive(NetIO &io, PP &pp, std::vector<block> &vec_X) 
{    
    if(vec_X.size() != pp.RECEIVER_ITEM_NUM){
        std::cerr << "input size of vec_X does not match public parameters" << std::endl;
        exit(1);  // EXIT_FAILURE  
    }
    
    PrintSplitLine('-'); 
    auto start_time = std::chrono::steady_clock::now(); 

    uint8_t k2[32];
    PRG::Seed seed = PRG::SetSeed(fixed_seed, 0); // initialize PRG
    GenRandomBytes(seed, k2, 32);  // pick a key k2

    std::vector<EC25519Point> vec_Hash_X(pp.RECEIVER_ITEM_NUM); 
    std::vector<EC25519Point> vec_Fk2_X(pp.RECEIVER_ITEM_NUM); 
    #pragma omp parallel for num_threads(NUMBER_OF_THREADS)
    for(auto i = 0; i < pp.RECEIVER_ITEM_NUM; i++){
        Hash::BlockToBytes(vec_X[i], vec_Hash_X[i].px, 32); 
        x25519_scalar_mulx(vec_Fk2_X[i].px, k2, vec_Hash_X[i].px); 
    } 

    // first receive incoming data
    std::vector<EC25519Point> vec_Fk1_Y(pp.SENDER_ITEM_NUM);
    io.ReceiveEC25519Points(vec_Fk1_Y.data(), pp.SENDER_ITEM_NUM); // receive Fk1_Y from Server

    // then send
    io.SendEC25519Points(vec_Fk2_X.data(), pp.RECEIVER_ITEM_NUM);

    std::cout <<"cwPRF-based PSI [step 2]: Receiver ===> F_k2(x_i) ===> Sender"; 

    std::cout << " [" << 32*pp.RECEIVER_ITEM_NUM/(1024*1024) << " MB]" << std::endl;

    std::vector<EC25519Point> vec_Fk2k1_Y(pp.SENDER_ITEM_NUM);
    #pragma omp parallel for num_threads(NUMBER_OF_THREADS)
    for(auto i = 0; i < pp.SENDER_ITEM_NUM; i++){
        x25519_scalar_mulx(vec_Fk2k1_Y[i].px, k2, vec_Fk1_Y[i].px); // (H(x_i)^k2)^k1
    }

    std::vector<std::string> vec_TRUNCATE_Fk1k2_X; 
    io.ReceiveStringVector(vec_TRUNCATE_Fk1k2_X, pp.TRUNCATE_LEN); 
    std::unordered_set<std::string> S;
    for(auto i = 0; i < pp.SENDER_ITEM_NUM; i++){
        S.insert(std::string(&vec_Fk2k1_Y[i].px[0], &vec_Fk2k1_Y[i].px[0]+pp.TRUNCATE_LEN)); 
    }

    std::vector<block> vec_intersection; 
    for(auto i = 0; i < pp.RECEIVER_ITEM_NUM; i++){
        if(S.find(vec_TRUNCATE_Fk1k2_X[i]) != S.end()){
            vec_intersection.emplace_back(vec_X[i]); 
        }
    }
    
    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "cwPRF-based PSI: Receiver side takes time = " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;

    PrintSplitLine('-'); 
    
    return vec_intersection; 
}

}
#endif

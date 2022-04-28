#ifndef KUNLUN_ALSZ_OTE_HPP_
#define KUNLUN_ALSZ_OTE_HPP_

#include "naor_pinkas_ot.hpp"
#include "../../utility/routines.hpp"
/*
 * ALSZ OT Extension
 * [REF] With optimization of "More Efficient Oblivious Transfer and Extensions for Faster Secure Computation"
 * https://eprint.iacr.org/2013/552.pdf
 */

const static size_t BASE_LEN = 128; // the default length of base OT

namespace ALSZOTE{

// check if the parameters are legal
void CheckParameters(size_t ROW_NUM, size_t COLUMN_NUM)
{
    if (ROW_NUM%128 != 0 || COLUMN_NUM%128 != 0){
        std::cerr << "row or colulumn parameters is wrong" << std::endl;
        exit(EXIT_FAILURE); 
    }
}

struct PP
{
    uint8_t malicious = 0; // false
    NPOT::PP baseOT;  
    size_t BASE_LEN = 128; // the default length of base OT  
};

void PrintPP(const PP &pp)
{
    std::cout << "malicious = " << int(pp.malicious) << std::endl; 
    NPOT::PrintPP(pp.baseOT);
}


// serialize pp to stream
std::ofstream &operator<<(std::ofstream &fout, const PP &pp)
{
	fout << pp.baseOT; 
    fout << pp.malicious; 
    fout << pp.BASE_LEN;
    return fout;
}


// deserialize pp from stream
std::ifstream &operator>>(std::ifstream &fin, PP &pp)
{
	fin >> pp.baseOT; 
    fin >> pp.malicious; 
    fin >> pp.BASE_LEN;
    return fin; 
}

PP Setup(size_t BASE_LEN)
{
    PP pp; 
    pp.malicious = 0; 
    pp.baseOT = NPOT::Setup();
    return pp;
}

// save pp to file
void SavePP(PP &pp, std::string pp_filename)
{
	std::ofstream fout; 
    fout.open(pp_filename, std::ios::binary); 
    if(!fout)
    {
        std::cerr << pp_filename << " open error" << std::endl;
        exit(1); 
    }
    fout << pp; 
    fout.close(); 
}


// fetch pp from file
void FetchPP(PP &pp, std::string pp_filename)
{
	std::ifstream fin; 
    fin.open(pp_filename, std::ios::binary); 
    if(!fin)
    {
        std::cerr << pp_filename << " open error" << std::endl;
        exit(1); 
    }
    fin >> pp; 
    fin.close(); 
}

void PrepareSend(NetIO &io, PP &pp, std::vector<block> &vec_K0, std::vector<block> &vec_K1, size_t EXTEND_LEN)
{
    /* 
    ** Phase 1: sender obtains a random blended matrix Q of matrix T and U from receiver
    ** T and U are tall and skinny matrix, to use base OT oblivious transfer T and U, 
    ** the sender first oblivous get 1-out-of-2 keys per column from receiver via base OT 
    ** receiver then send encryptions of the original column and shared column under k0 and k1 respectively
    */

    // prepare to receive a secret shared matrix Q from receiver
    size_t ROW_NUM = EXTEND_LEN;   // set row num as the length of long ot
    size_t COLUMN_NUM = pp.BASE_LEN;  // set column num as the length of base ot

    CheckParameters(ROW_NUM, COLUMN_NUM); 

    PRG::Seed seed = PRG::SetSeed(nullptr, 0); // initialize PRG seed

    // generate Phase 1 selection bit vector
    std::vector<uint8_t> vec_sender_selection_bit = PRG::GenRandomBits(seed, COLUMN_NUM); 

    // first receive 1-out-2 two keys from the receiver 
    std::vector<block> vec_inner_K = NPOT::Receive(io, pp.baseOT, vec_sender_selection_bit, COLUMN_NUM);

    std::cout << "ALSZ OTE [step 1]: Sender obliviously get " << BASE_LEN 
              << " number of keys from Receiver via base OT" << std::endl; 
    /* 
    ** invoke base OT COLUMN_NUM times to obtain a matrix Q
    ** after receiving the key, begin to receive ciphertexts
    */

    std::vector<block> vec_inner_C0(COLUMN_NUM); // 1 block = 128 bits 
    std::vector<block> vec_inner_C1(COLUMN_NUM); 

    io.ReceiveBlocks(vec_inner_C0.data(), COLUMN_NUM); 
    io.ReceiveBlocks(vec_inner_C1.data(), COLUMN_NUM);

    std::vector<block> vec_Q_seed(COLUMN_NUM); 

    for(auto j = 0; j < COLUMN_NUM; j++){
        if(vec_sender_selection_bit[j] == 0){
            vec_Q_seed[j] = vec_inner_C0[j]^vec_inner_K[j];
        }
        else{
            vec_Q_seed[j] = vec_inner_C1[j]^vec_inner_K[j];
        } 
    } 

    #ifdef DEBUG
        std::cout << "ALSZ OTE: Sender obliviuosly get "<< COLUMM_NUM << " number of seeds from Receiver" << std::endl; 
    #endif

    std::vector<block> Q; // size = ROW_NUM/128 * COLUMN_NUM 
    std::vector<block> Q_column; // size = ROW_NUM/128
    for(auto j = 0; j < COLUMN_NUM; j++){
        PRG::ReSeed(seed, &vec_Q_seed[j], 0); 
        Q_column = PRG::GenRandomBlocks(seed, ROW_NUM/128);
        Q.insert(Q.end(), Q_column.begin(), Q_column.end());  
    } 
    // transpose Q 
    std::vector<block> Q_transpose(ROW_NUM/128 * COLUMN_NUM);  
    BitMatrixTranspose((uint8_t*)Q.data(), COLUMN_NUM, ROW_NUM, (uint8_t*)Q_transpose.data());  

    std::vector<block> P(ROW_NUM/128 * COLUMN_NUM); 
    io.ReceiveBlocks(P.data(), ROW_NUM/128 * COLUMN_NUM); 
    // transpose P
    std::vector<block> P_transpose(ROW_NUM/128 * COLUMN_NUM); 
    BitMatrixTranspose((uint8_t*)P.data(), COLUMN_NUM, ROW_NUM, (uint8_t*)P_transpose.data());  

    #ifdef DEBUG
        std::cout << "ALSZ OTE: Sender transposes matrix Q and P" << std::endl; 
    #endif

    // generate dense representation of selection block
    std::vector<block> vec_sender_selection_block(COLUMN_NUM/128); 
    Block::FromSparseBytes(vec_sender_selection_bit.data(), COLUMN_NUM, vec_sender_selection_block.data(), COLUMN_NUM/128); 


    // begin to transmit the real message
    std::vector<block> vec_outer_C0(ROW_NUM); 
    std::vector<block> vec_outer_C1(ROW_NUM); 

    //#pragma omp parallel for
    for(auto i = 0; i < ROW_NUM; i++)
    {
        std::vector<block> Q_row(COLUMN_NUM/128);
        memcpy(Q_row.data(), Q_transpose.data()+i*COLUMN_NUM/128, COLUMN_NUM/8); 

        std::vector<block> P_row(COLUMN_NUM/128);
        memcpy(P_row.data(), P_transpose.data()+i*COLUMN_NUM/128, COLUMN_NUM/8); 
        
        std::vector<block> vec_adjust = Block::AND(vec_sender_selection_block, P_row); 

        Q_row = Block::XOR(Q_row, vec_adjust);

        vec_K0[i] = Hash::BlocksToBlock(Q_row); 
        vec_K1[i] = Hash::BlocksToBlock(Block::XOR(Q_row, vec_sender_selection_block));
    }
}

void PrepareReceive(NetIO &io, PP &pp, std::vector<block> &vec_K, 
                    std::vector<uint8_t> &vec_receiver_selection_bit, size_t EXTEND_LEN)
{
    // prepare a random matrix
    size_t ROW_NUM = EXTEND_LEN; 
    size_t COLUMN_NUM = pp.BASE_LEN; 

    CheckParameters(ROW_NUM, COLUMN_NUM); 

    PRG::Seed seed = PRG::SetSeed(nullptr, 0); 

    // generate two seed vector to generate two pseudorandom matrixs 
    std::vector<block> vec_T_seed = PRG::GenRandomBlocks(seed, COLUMN_NUM);
    std::vector<block> vec_U_seed = PRG::GenRandomBlocks(seed, COLUMN_NUM);
    
    // block representations for matrix T, U, and P: size = ROW_NUM/128*COLUMN_NUM
    std::vector<block> T, T_column; 
    std::vector<block> U, U_column; 
    std::vector<block> P, P_column; 

    // generate the dense representation of selection block
    std::vector<block> vec_receiver_selection_block(ROW_NUM/128); 
    Block::FromSparseBytes(vec_receiver_selection_bit.data(), ROW_NUM, vec_receiver_selection_block.data(), ROW_NUM/128); 
    
    for(auto j = 0; j < COLUMN_NUM; j++){
        // generate two random matrixs
        PRG::ReSeed(seed, &vec_T_seed[j], 0); 
        T_column = PRG::GenRandomBlocks(seed, ROW_NUM/128);
        T.insert(T.end(), T_column.begin(), T_column.end()); 
        PRG::ReSeed(seed, &vec_U_seed[j], 0);  
        U_column = PRG::GenRandomBlocks(seed, ROW_NUM/128); 
        U.insert(U.end(), T_column.begin(), T_column.end()); 
        
        // generate adjust matrix  
        std::vector<block> P_column = Block::XOR(T_column, U_column);
        P_column = Block::XOR(P_column, vec_receiver_selection_block); 
        P.insert(P.end(), P_column.begin(), P_column.end()); 
    } 

    // generate COLUMN pair of keys
    std::vector<block> vec_inner_K0 = PRG::GenRandomBlocks(seed, COLUMN_NUM);
    std::vector<block> vec_inner_K1 = PRG::GenRandomBlocks(seed, COLUMN_NUM);

    // Phase 1: first transmit 1-out-2 key to sender    
    NPOT::Send(io, pp.baseOT, vec_inner_K0, vec_inner_K1, COLUMN_NUM); 

    std::cout << "ALSZ OTE [step 1]: Receiver transmits "<< COLUMN_NUM << " number of keys to Sender via base OT" 
              << std::endl; 

    // Phase 1: transmit ciphertext a.k.a. encypted seeds
    std::vector<block> vec_inner_C0(COLUMN_NUM); 
    std::vector<block> vec_inner_C1(COLUMN_NUM); 
    
    #pragma omp parallel for
    for(auto j = 0; j < COLUMN_NUM; j++)
    {
        vec_inner_C0[j] = vec_inner_K0[j]^vec_T_seed[j]; 
        vec_inner_C1[j] = vec_inner_K1[j]^vec_U_seed[j];
    }   
    io.SendBlocks(vec_inner_C0.data(), COLUMN_NUM); 
    io.SendBlocks(vec_inner_C1.data(), COLUMN_NUM);

    std::cout << "ALSZ OTE [step 2]: Receiver ===> 2*" << COLUMN_NUM << " encrypted seeds ===> Sender" 
              << " [" << (double)COLUMN_NUM*16*2/(1024*1024) << " MB]" << std::endl;

    // Phase 1: transmit adjust bit matrix
    io.SendBlocks(P.data(), ROW_NUM/128*COLUMN_NUM); 
    std::cout << "ALSZ OTE [step 2]: Receiver ===> " << ROW_NUM << "*" << COLUMN_NUM << " adjust bit matrix ===> Sender" 
              << " [" << (double)ROW_NUM/128*COLUMN_NUM*16*2/(1024*1024) << " MB]" << std::endl;

    // transpose T
    std::vector<block> T_transpose(ROW_NUM/128 * COLUMN_NUM); 
    BitMatrixTranspose((uint8_t*)T.data(), COLUMN_NUM, ROW_NUM, (uint8_t*)T_transpose.data());

    #ifdef DEBUG
        std::cout << "ALSZ OTE: Receiver transposes matrix T" << std::endl; 
    #endif

    //#pragma omp parallel for
    for(auto i = 0; i < ROW_NUM; i++)
    {
        std::vector<block> T_row(COLUMN_NUM/128);  
        memcpy(T_row.data(), T_transpose.data()+i*COLUMN_NUM/128, COLUMN_NUM/8); 

        vec_K[i] = Hash::BlocksToBlock(T_row); 
    }   
}

void Send(NetIO &io, PP &pp, std::vector<block> &vec_m0, std::vector<block> &vec_m1, size_t EXTEND_LEN) 
{
    /* 
    ** Phase 1: sender obtains a random secret sharing matrix Q of matrix T from receiver
    ** T is a tall matrix, to use base OT oblivious transfer T, 
    ** the sender first oblivous get 1-out-of-2 keys per column from receiver via base OT 
    ** receiver then send encryptions of the original column and shared column under k0 and k1 respectively
    */
    PrintSplitLine('-'); 
	auto start_time = std::chrono::steady_clock::now(); 

    // prepare to receive a secret shared matrix Q from receiver
    size_t ROW_NUM = EXTEND_LEN;   // set row num as the length of long ot
    size_t COLUMN_NUM = pp.BASE_LEN;  // set column num as the length of base ot

    CheckParameters(ROW_NUM, COLUMN_NUM); 
    
    std::vector<block> vec_K0(ROW_NUM); 
    std::vector<block> vec_K1(ROW_NUM);

    PrepareSend(io, pp, vec_K0, vec_K1, EXTEND_LEN);  

    // begin to transmit the real message
    std::vector<block> vec_outer_C0(ROW_NUM); 
    std::vector<block> vec_outer_C1(ROW_NUM); 

    #pragma omp parallel for
    for(auto i = 0; i < ROW_NUM; i++)
    {       
        vec_outer_C0[i] = vec_m0[i]^vec_K0[i]; 
        vec_outer_C1[i] = vec_m1[i]^vec_K1[i];
    }
    io.SendBlocks(vec_outer_C0.data(), ROW_NUM); 
    io.SendBlocks(vec_outer_C1.data(), ROW_NUM);

    
    std::cout << "ALSZ OTE [step 3]: Sender ===> (vec_C0, vec_C1) ===> Receiver" 
              << "[" << (double)ROW_NUM*16*2/(1024*1024) << " MB]" << std::endl; 

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "ALSZ OTE: Sender side takes time " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;
    PrintSplitLine('-'); 
}


std::vector<block> Receive(NetIO &io, PP &pp, std::vector<uint8_t> &vec_receiver_selection_bit, size_t EXTEND_LEN)
{
    PrintSplitLine('-'); 
  
    auto start_time = std::chrono::steady_clock::now(); 

    // prepare a random matrix
    size_t ROW_NUM = EXTEND_LEN; 
    size_t COLUMN_NUM = pp.BASE_LEN; 

    CheckParameters(ROW_NUM, COLUMN_NUM); 

    // first act as sender in base OT
    std::vector<block> vec_K(ROW_NUM); 
    PrepareReceive(io, pp, vec_K, vec_receiver_selection_bit, EXTEND_LEN); 

    // receiver real payloads
    std::vector<block> vec_outer_C0(ROW_NUM); 
    std::vector<block> vec_outer_C1(ROW_NUM); 

    io.ReceiveBlocks(vec_outer_C0.data(), ROW_NUM);
    io.ReceiveBlocks(vec_outer_C1.data(), ROW_NUM);

    #ifdef DEBUG
        std::cout << "ALSZ OTE: Receiver get "<< ROW_NUM << " pair of ciphertexts from Sender" << std::endl; 
    #endif

    std::vector<block> vec_result(ROW_NUM);
    #pragma omp parallel for
    for(auto i = 0; i < ROW_NUM; i++)
    {
        if(vec_receiver_selection_bit[i] == 0){
            vec_result[i] = vec_outer_C0[i]^vec_K[i]; 
        }
        else{
            vec_result[i] = vec_outer_C1[i]^vec_K[i];
        }
    }   

    #ifdef DEBUG
        std::cout << "ALSZ OTE: Receiver obtains "<< ROW_NUM << " number of messages from Sender" << std::endl; 
        PrintSplitLine('*'); 
    #endif

    std::cout << "ALSZ OTE [step 4]: Receiver obtains vec_m" << std::endl; 

    auto end_time = std::chrono::steady_clock::now(); 
    auto running_time = end_time - start_time;
    std::cout << "ALSZ OTE: Receiver side takes time " 
              << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;

    PrintSplitLine('-'); 

    return vec_result; 
}

}
#endif
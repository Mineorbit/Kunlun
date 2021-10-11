//#define DEBUG

#include "../crypto/ec_point.hpp"
#include "../pke/twisted_elgamal.hpp"
#include "../nizk/nizk_plaintext_knowledge.hpp"


void GenRandomEncInstanceWitness(PlaintextKnowledge::PP &pp, PlaintextKnowledge::Instance &instance, 
                                 PlaintextKnowledge::Witness &witness)
{
    PrintSplitLine('-');  
    std::cout << "generate a valid twisted elgamal ciphertext >>>" << std::endl; 

    witness.r = GenRandomBigIntLessThan(order); 
    witness.v = GenRandomBigIntLessThan(order);

    instance.pk = GenRandomGenerator(); 
    TwistedElGamal::PP pp_enc; 
    pp_enc.g = pp.g; 
    pp_enc.h = pp.h;  

    TwistedElGamal::CT ct; 
    TwistedElGamal::Enc(pp_enc, instance.pk, witness.v, witness.r, ct); 
    instance.X = ct.X; 
    instance.Y = ct.Y;
}

void test_nizk_plaintext_knowledge()
{
    std::cout << "begin the test of NIZKPoK for plaintext knowledge >>>" << std::endl; 
    
    PlaintextKnowledge::PP pp; 
    PlaintextKnowledge::Setup(pp);
    PlaintextKnowledge::Instance instance;
    PlaintextKnowledge::Witness witness; 
    PlaintextKnowledge::Proof proof; 

    GenRandomEncInstanceWitness(pp, instance, witness); 

    std::string transcript_str; 

    auto start_time = std::chrono::steady_clock::now(); // start to count the time
    transcript_str = ""; 
    PlaintextKnowledge::Prove(pp, instance, witness, transcript_str, proof); 
    auto end_time = std::chrono::steady_clock::now(); // end to count the time
    auto running_time = end_time - start_time;
    std::cout << "proof generation takes time = " 
    << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;

    start_time = std::chrono::steady_clock::now(); // start to count the time
    transcript_str = ""; 
    PlaintextKnowledge::Verify(pp, instance, transcript_str, proof); 
    end_time = std::chrono::steady_clock::now(); // end to count the time
    running_time = end_time - start_time;
    std::cout << "proof verification takes time = " 
    << std::chrono::duration <double, std::milli> (running_time).count() << " ms" << std::endl;
}

int main()
{
    Context_Initialize(); 
    ECGroup_Initialize(NID_X9_62_prime256v1);   
    
    test_nizk_plaintext_knowledge();

    ECGroup_Finalize(); 
    Context_Finalize(); 

    return 0; 
}


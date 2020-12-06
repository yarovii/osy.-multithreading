#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */

enum thread_type {  RECEIVER,       //0
    TRANSMITTER,    //1
    WORKER         //2
};

struct MessageData
{
    uint32_t                    m_ID;
    std::vector<uint64_t>  m_Fragments;
    bool complete;
};
struct MessageComplete
{
    uint32_t                    m_ID;
    CBigInt                 m_Result;
};

class CSentinelHacker     //////// DONT FORGET TO MAKE CONSTRUCTOR!!!!!
    /////  LAG ESLI BOLSHE MESSAGE CHEM NUZNO !!!!!
{
    condition_variable cv_frags;
    condition_variable cv_transmitters;
    condition_variable cv_add_fragments;
    mutex m_frags;
    mutex m_m_data;
    mutex m_transmitters;
    mutex m_workers;
    mutex m_add_fragments;
    mutex m_erase_index;
    mutex m_message_complete;
    atomic<size_t> count_receivers;
    atomic<size_t> count_workers;
    atomic<size_t> count_add_fragments;
//    atomic<size_t> count_tmp=0;    //////DELETE

//    int counter_tmp=0;                  //////// DELETE THIS FOR STOP EXAMPLES

    deque<MessageData> message_data;
    deque<MessageComplete> message_complete;
    deque<MessageData*> message_index;

//    map<int, int> count_id;             /////// FOR MAP EXAMPLE
//    std::map<int, int>::iterator it;    /////// SAME AS PREV

    bool stop;
    bool receivers_done;

    vector<ATransmitter> transmitters;
    vector<AReceiver> receivers;
    set<pair<thread_type, thread*>> thread_pool;
    deque<uint64_t> new_fragments;
    deque<uint64_t> add_fragments;

public:
    static bool              SeqSolve                      ( const vector<uint64_t> & fragments,
                                                             CBigInt         & res );
    void                     AddTransmitter                ( ATransmitter      x );
    void                     AddReceiver                   ( AReceiver         x );
    void                     AddFragment                   ( uint64_t          x );
    void                     Start                         ( unsigned          thrCount );
    void                     Stop                          ( void );
    void findFragmentId(uint64_t number, uint32_t &id);   ////// FIX THIS SHEET
    void addMessageData(uint64_t &number, uint32_t &id);
    void ReceiverFunc(size_t i);
    void TransmitterFunc(size_t i);
    void WorkerFunc();

    CSentinelHacker(){
        count_receivers=0;
        count_workers=0;
        count_add_fragments=0;

        stop=false;
        receivers_done=false;
    }

};
// TODO: CSentinelHacker implementation goes here

void CSentinelHacker::AddTransmitter ( ATransmitter      x ){
    transmitters.push_back(x);
}

void CSentinelHacker::AddReceiver ( AReceiver         x ){
    receivers.push_back(x);
}

void CSentinelHacker::AddFragment ( uint64_t          x ){
    if(!stop) {
//        printf("%i\n", ++count_add_fragments);
        ++count_add_fragments;
        m_add_fragments.lock();
        add_fragments.push_back(x);
        m_add_fragments.unlock();
        cv_frags.notify_one();
        cv_add_fragments.notify_one();
    }
//    }
}

void CSentinelHacker::ReceiverFunc(size_t i) {
//    printf("Thread active ReceiverFunc %i \n", this_thread::get_id());

    uint64_t frag = 0;
    bool rc = false;

    while ( 1 ) {
        rc = receivers.at(i)->Recv(frag);

        if ( !rc ) break;

        m_frags.lock();
        new_fragments.push_back(frag);
        m_frags.unlock();
        cv_frags.notify_one();
    }
    if(++count_receivers == receivers.size())
        receivers_done = true;
}

void CSentinelHacker::TransmitterFunc(size_t i) {
//    cout << "Thread active TransmitterFunc " << this_thread::get_id()<<endl;
//    printf("Thread active TransmitterFunc %i\n", this_thread::get_id());
//    this_thread::sleep_for(chrono::seconds(1));

    while(1) {
        unique_lock <std::mutex> locker(m_transmitters);
        cv_transmitters.wait(locker, [this]() {
            return (message_complete.size() > 0 || count_workers == 0) ;});

        if( count_workers == 0 && message_complete.size() == 0 && message_index.size() == 0) {
            locker.unlock();
            cv_transmitters.notify_all();
            return;
        }

        if ( message_complete.size() > 0) {
            m_message_complete.lock();
            transmitters.at(i)->Send(message_complete.front().m_ID, message_complete.front().m_Result);
            message_complete.pop_front();
            m_message_complete.unlock();
            locker.unlock();
        }
        else if(count_workers == 0 && message_index.size() > 0 && message_complete.size() == 0){
            m_erase_index.lock();
            if(!message_index.front()->complete)
                transmitters.at(i)->Incomplete(message_index.front()->m_ID);
            message_index.pop_front();
            m_erase_index.unlock();
            cv_transmitters.notify_all();
            locker.unlock();
        }
    }
}

void CSentinelHacker::WorkerFunc() {
//    printf("Thread active WorkerFunc %i \n", this_thread::get_id());
    uint64_t frag = 0;
    while(1){
        unique_lock<std::mutex> locker(m_workers);
        cv_frags.wait(locker, [this](){ return (new_fragments.size() > 0) || add_fragments.size() > 0 || (stop && receivers_done);});

        if(stop) {
            if ( receivers_done && add_fragments.size() == 0 && count_add_fragments == 0 && new_fragments.size() == 0 ) {
                locker.unlock();
//                printf("Thread active TransmitterFunc %i\n", ++count_tmp);
                cv_frags.notify_all();
                --count_workers;
                cv_transmitters.notify_all();
//                printf("Thread WorkerFunc %i \n", this_thread::get_id());
                return;
            }  /////// ADD IF NEW FRAGMENTS DOESNT EXIST BUT ADDFRAGMENTS HAS; IT MUST WAIT FOR IT
        }

        bool found = false;
        if(new_fragments.size() > 0){
            m_frags.lock();
            frag = new_fragments.front();
            new_fragments.pop_front();
            m_frags.unlock();
            locker.unlock();
            found = true;
        }
        else if ( add_fragments.size() > 0 ) {
            m_add_fragments.lock();
            frag = add_fragments.front();
            add_fragments.pop_front();
            --count_add_fragments;
            m_add_fragments.unlock();
            locker.unlock();
            found = true;
        }
        else if ( count_add_fragments > 0 ) {
            unique_lock <std::mutex> lock(m_add_fragments);
            if ( add_fragments.size() == 0 ) {
                cv_add_fragments.wait(lock);
            }
            frag = add_fragments.front();
            add_fragments.pop_front();
            --count_add_fragments;
            lock.unlock();
            locker.unlock();
            found = true;
        }

        if(found){
            uint32_t id = 0;
            findFragmentId(frag, id);

            ////// Testing map
            /*it = count_id.find(id);
            if ( it != count_id.end())
                it->second += 1;
            else
                count_id.insert(make_pair(id, 1));
*/
            /////

            bool contains = false;
            size_t index=0;
            m_m_data.lock();
            for ( size_t i = 0; i <  message_data.size(); i++ ) {
                if ( !message_data[i].complete && message_data[i].m_ID == id ) {
                    message_data[i].m_Fragments.push_back(frag);
                    contains = true;
                    index = i;
                    break;
                }
            }

            if ( !contains ) {
                MessageData m;
                m.m_ID = id;
                m.m_Fragments.push_back(frag);
                m.complete = false;
                message_data.push_back(m);                      ///////LOCK MESS_INDEX HERE!

                m_erase_index.lock();
                message_index.push_back(&message_data.back());
                m_erase_index.unlock();

                index = message_data.size() - 1;
            }
            m_m_data.unlock();

            CBigInt res;
            bool solved = SeqSolve(message_data[index].m_Fragments, res);

            if ( solved ) {
                message_data[index].complete = true;

                MessageComplete m;
                m.m_ID = id;
                m.m_Result = res;

                m_message_complete.lock();
                message_complete.push_back(m);
                m_message_complete.unlock();

                cv_transmitters.notify_one();
            }
        }
    }
}

bool CSentinelHacker::SeqSolve( const vector<uint64_t> & fragments, CBigInt         & res ){

    uint32_t t = FindPermutations(fragments.data(), fragments.size(), [&](const uint8_t * message_, size_t size){
        res = CountExpressions(&message_[4], size-32);
    });
    if(t > 0)
        return true;

    return false;
}

void CSentinelHacker::findFragmentId(uint64_t fragment, uint32_t &id ){
    char bitset[64];
    char id_c[27];
    int j=0;
    uint64_t number = fragment;
    for(size_t i=0; i<64; ++i)
    {
        if((number & 1) != 0)
            bitset[64-i] = '1';
        else
            bitset[64-i] = '0';

        number >>= 1;

        if(i > 36)
            id_c[j++] = bitset[64-i];
    }

    for (int stringpos=26; stringpos>=0; stringpos--) {
        id = id<<1;
        if (id_c[stringpos]=='1') id += 1;
    }
}

void CSentinelHacker::Start ( unsigned          thrCount ){

    size_t size = receivers.size();
    count_workers = thrCount;
    size_t index=0;
    thread *th;
    while(1) {
        th = new thread(&CSentinelHacker::ReceiverFunc,this, index);
        thread_pool.insert(pair<thread_type, thread*>(RECEIVER, th));
        --size;
        if(size == 0)break;
        ++index;
    }
    size = thrCount;
    while(size != 0){
        th =new thread(&CSentinelHacker::WorkerFunc,this);
        thread_pool.insert(pair<thread_type, thread*>(WORKER, th));
        --size;
    }

    size = transmitters.size();
    index =0;
    while(size != 0){
        th =new thread(&CSentinelHacker::TransmitterFunc,this, index);
        thread_pool.insert(pair<thread_type, thread*>(TRANSMITTER, th));
        --size;
        ++index;
    }
}

void CSentinelHacker::Stop ( void )                         //////// FREE ALL THREADS PIDOR
{
    thread *th;
    stop = true;
    cv_add_fragments.notify_all();
    cv_frags.notify_one();

//    counter_tmp=0;
    for (auto const &x : thread_pool) {
        th = x.second;
        th->join();/*
        cout << "(" << x.first << ", "
             << x.second << ")"
             << " ";
        counter_tmp++;*/
    }
    /*for ( size_t i = 0; i <  message_data.size(); i++ ) {
        cout << "m ID   " <<  message_data[i].m_ID << endl;
    }*/
}



//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int                main                                    ( void )
{
    using namespace std::placeholders;
    /*for ( const auto & x : g_TestSets )
    {
      CBigInt res;
      assert ( CSentinelHacker::SeqSolve ( x . m_Fragments, res ) );
      assert ( CBigInt ( x . m_Result ) . CompareTo ( res ) == 0 );
    }*/
    CSentinelHacker test;
////  auto            trans = make_shared<CExampleTransmitter> ();
//  AReceiver       recv  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x02230000000c, 0x071e124dabef, 0x02360037680e, 0x071d2f8fe0a1, 0x055500150755 } );
//  AReceiver       recv1  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x071f6b8342ab, 0x0738011f538d, 0x0732000129c3, 0x055e6ecfa0f9, 0x02ffaa027451, 0x02280000010b, 0x02fb0b88bc3e } );
//  AReceiver       recv2  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x073700609bbd, 0x055901d61e7b, 0x022a0000032b, 0x016f0000edfb } );
//  AReceiver       recv12  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x017f4cb42a68, 0x02260000000d, 0x072500000025 } );
//  AReceiver       recv14 = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x00000000000c, 0x071e124dabef, 0x02360037680e, 0x071d2f8fe0a1, 0x055500150755 } );
//  AReceiver       recv13  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x00000000000c, 0x071e124dabef, 0x02360037680e, 0x071d2f8fe0a1, 0x055500150755 } );

//  test . AddTransmitter ( trans );
//  cout << "here";
//  test . AddReceiver ( recv );
//  test . AddReceiver ( recv1 );
// test . AddReceiver ( recv2 );
//  test . AddReceiver ( recv12 );
//   test . AddReceiver ( recv13 );
//  test . AddReceiver ( recv14 );
//    cout << "here";
//  test . Start ( 3 );
//  test . Start ( 3);
//  test . Stop ();

//  static initializer_list<uint64_t> t1Data = { 0x071f6b8342ab, 0x0738011f538d, 0x0732000129c3, 0x055e6ecfa0f9, 0x02ffaa027451, 0x02280000010b, 0x02fb0b88bc3e };
//  thread t1 ( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), t1Data );
//
//  static initializer_list<uint64_t> t2Data = { 0x073700609bbd, 0x055901d61e7b, 0x022a0000032b, 0x016f0000edfb };
//  thread t2 ( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), t2Data );
//  FragmentSender ( bind ( &CSentinelHacker::AddFragment, &test, _1 ), initializer_list<uint64_t> { 0x017f4cb42a68, 0x02260000000d, 0x072500000025 } );
//  t1 . join ();
//  t2 . join ();
//  test . Stop ();
//  assert ( trans -> TotalSent () == 4 );
//  assert ( trans -> TotalIncomplete () == 2 );
    /*for ( const auto & x : g_TestSets )
    {
        CBigInt res;
        assert ( CSentinelHacker::SeqSolve ( x . m_Fragments, res ) );
        assert ( CBigInt ( x . m_Result ) . CompareTo ( res ) == 0 );
    }*/

//    CSentinelHacker test;
    auto            trans = make_shared<CExampleTransmitter> ();
    AReceiver       recv  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x02230000000c, 0x071e124dabef, 0x02360037680e, 0x071d2f8fe0a1, 0x055500150755 } );
    AReceiver       recv0  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x02230000000c, 0x071e124dabef } );
    AReceiver       recv1  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x02360037680e, 0x071d2f8fe0a1, 0x055500150755 } );
//
    test . AddTransmitter ( trans );
    test . AddReceiver ( recv );
    /*test . AddReceiver ( recv0 );
    test . AddReceiver ( recv1 );*/
    test . Start ( 2 );
    vector<thread*> th;
//    thread tg;
    for ( const auto & x : g_TestSets )
    {
        th.push_back(new thread( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), x . m_Fragments ));
    }
    for(size_t i=0; i < th.size(); i++)
        th.at(i)->join();
//    test . Start ( 3 );


    /*static initializer_list<uint64_t> t1Data = { 0x071f6b8342ab, 0x0738011f538d, 0x0732000129c3, 0x055e6ecfa0f9, 0x02ffaa027451, 0x02280000010b, 0x02fb0b88bc3e };
    thread t1 ( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), t1Data );

//    static initializer_list<uint64_t> t2Data = { 0x073700609bbd, 0x055901d61e7b, 0x022a0000032b, 0x016f0000edfb };
    static initializer_list<uint64_t> t2Data = { 0x073700609bbd, 0x055901d61e7b, 0x022a0000032b, 0x016f0000edfb, 0x017f4cb42a68, 0x02260000000d, 0x072500000025 };
    thread t2 ( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), t2Data );
//    FragmentSender ( bind ( &CSentinelHacker::AddFragment, &test, _1 ), initializer_list<uint64_t> { 0x017f4cb42a68, 0x02260000000d, 0x072500000025 } );
    t1 . join ();
    t2 . join ();*/
    test . Stop ();
//    assert ( trans -> TotalSent () == 4 );
//    assert ( trans -> TotalIncomplete () == 2 );
//  cout << "Faggoe" << endl;
    return 0;
}

/*void test_main(){
    using namespace std::placeholders;
    for ( const auto & x : g_TestSets )
    {
        CBigInt res;
        assert ( CSentinelHacker::SeqSolve ( x . m_Fragments, res ) );
        assert ( CBigInt ( x . m_Result ) . CompareTo ( res ) == 0 );
    }

    CSentinelHacker test;
    auto            trans = make_shared<CExampleTransmitter> ();
    AReceiver       recv  = make_shared<CExampleReceiver> ( initializer_list<uint64_t> { 0x02230000000c, 0x071e124dabef, 0x02360037680e, 0x071d2f8fe0a1, 0x055500150755 } );

    test . AddTransmitter ( trans );
    test . AddReceiver ( recv );
    test . Start ( 3 );

    static initializer_list<uint64_t> t1Data = { 0x071f6b8342ab, 0x0738011f538d, 0x0732000129c3, 0x055e6ecfa0f9, 0x02ffaa027451, 0x02280000010b, 0x02fb0b88bc3e };
    thread t1 ( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), t1Data );

    static initializer_list<uint64_t> t2Data = { 0x073700609bbd, 0x055901d61e7b, 0x022a0000032b, 0x016f0000edfb };
    thread t2 ( FragmentSender, bind ( &CSentinelHacker::AddFragment, &test, _1 ), t2Data );
    FragmentSender ( bind ( &CSentinelHacker::AddFragment, &test, _1 ), initializer_list<uint64_t> { 0x017f4cb42a68, 0x02260000000d, 0x072500000025 } );
    t1 . join ();
    t2 . join ();
    test . Stop ();
    assert ( trans -> TotalSent () == 4 );
    assert ( trans -> TotalIncomplete () == 2 );
}*/
#endif /* __PROGTEST__ */ 

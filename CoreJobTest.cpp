#include "stdafx.h"
#include "CoreJobTest.h"
#include "CoreJob.h"
#include "move_only.h"

TEST_F(CoreJobTest, VerifySimpleProperties) {
  AutoRequired<CoreJob> jb;

  ASSERT_FALSE(m_create->IsInitiated()) << "CoreJob reported it could receive events before its enclosing context was created";

  // Create a thread which will delay for acceptance, and then quit:
  boost::thread t([this] {
    m_create->DelayUntilInitiated();
  });

  // Verify that this thread doesn't back out right away:
  ASSERT_FALSE(t.try_join_for(boost::chrono::milliseconds(10))) << "CoreJob did not block a client who was waiting for its readiness to accept dispatchers";

  // Now start the context and verify that certain properties changed as anticipated:
  m_create->Initiate();
  ASSERT_TRUE(m_create->DelayUntilInitiated()) << "CoreJob did not correctly delay for dispatch acceptance";

  // Verify that the blocked thread has become unblocked and quits properly:
  ASSERT_TRUE(t.try_join_for(boost::chrono::seconds(1))) << "CoreJob did not correctly signal a blocked thread that it was ready to accept dispatchers";
}

TEST_F(CoreJobTest, VerifySimpleSubmission) {
  AutoRequired<CoreJob> jb;
  
  auto myFlag = std::make_shared<bool>(false);
  *jb += [myFlag] {
    *myFlag = true;
  };

  // Kickoff, signal for a shutdown to take place, and then verify the flag
  AutoCurrentContext ctxt;
  ctxt->Initiate();
  ctxt->SignalShutdown(true);
  ASSERT_TRUE(*myFlag) << "CoreJob did not properly execute its thread";
}

TEST_F(CoreJobTest, VerifyTeardown) {
  AutoRequired<CoreJob> job;
  AutoCurrentContext ctxt;
  
  bool check1 = false;
  bool check2 = false;
  bool check3 = false;
  
  *job += [&check1] {
    boost::this_thread::sleep(boost::posix_time::milliseconds(200));
    check1 = true;
  };
  *job += [&check2] {
    boost::this_thread::sleep(boost::posix_time::milliseconds(200));
    check2 = true;
  };
  ctxt->Initiate();
  *job += [&check3] {
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    check3 = true;
  };
  
  ctxt->SignalShutdown(true);
  EXPECT_TRUE(check1) << "Lambda 1 didn't finish";
  EXPECT_TRUE(check2) << "Lambda 2 didn't finish";
  EXPECT_TRUE(check3) << "Lambda 3 didn't finish";
}

struct SimpleListen:
  virtual EventReceiver
{
  SimpleListen():
    m_flag(false)
  {}
  
  void SetFlag(){m_flag=true;}
  
  bool m_flag;
};

TEST_F(CoreJobTest, VerifyNoEventReceivers){
  AutoCreateContext ctxt1;
  CurrentContextPusher pshr1(ctxt1);
  
  AutoFired<SimpleListen> fire;
  ctxt1->Initiate();
  
  AutoCreateContext ctxt2;
  CurrentContextPusher pshr2(ctxt2);
  
  AutoRequired<SimpleListen> listener;
  ASSERT_FALSE(listener->m_flag) << "Flag was initialized improperly";
  
  fire(&SimpleListen::SetFlag)();
  EXPECT_FALSE(listener->m_flag) << "Lister recived event event though it wasn't initiated";
}

class CanOnlyMove {
public:
  CanOnlyMove(){}
  ~CanOnlyMove(){}
  CanOnlyMove(const CanOnlyMove& derp) = delete;
  CanOnlyMove(CanOnlyMove&& derp){}
};

TEST_F(CoreJobTest, MoveOnly){
  CanOnlyMove move;
  //CanOnlyMove derp = move; //error
  
  MoveOnly<CanOnlyMove> mo(std::move(move));
  
  MoveOnly<CanOnlyMove> first = mo;
  //MoveOnly<CanOnlyMove> second = mo; //error
}
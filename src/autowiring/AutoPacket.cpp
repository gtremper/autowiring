// Copyright (C) 2012-2014 Leap Motion, Inc. All rights reserved.
#include "stdafx.h"
#include "AutoPacket.h"
#include "Autowired.h"
#include "AutoPacketFactory.h"
#include "AutoPacketProfiler.h"
#include "ContextEnumerator.h"
#include "SatCounter.h"
#include <list>

#include <iostream>

AutoPacket::AutoPacket(AutoPacketFactory& factory, const std::shared_ptr<Object>& outstanding):
  m_outstandingRemote(outstanding)
{
  // Traverse all contexts, adding their packet subscriber vectors one at a time:
  for(const auto& curContext : ContextEnumerator(factory.GetContext())) {
    AutowiredFast<AutoPacketFactory> curFactory(curContext);
    if(curFactory)
      // Only insert if this context actually has a packet factory
      curFactory->AppendAutoFiltersTo(m_satCounters);
  }

  // Sort, eliminate duplicates
  std::sort(m_satCounters.begin(), m_satCounters.end());
  m_satCounters.erase(std::unique(m_satCounters.begin(), m_satCounters.end()), m_satCounters.end());

  // Prime the satisfaction graph for each element:
  for(auto& satCounter : m_satCounters) {
    for(
      auto pCur = satCounter.GetAutoFilterInput();
      *pCur;
      pCur++
    ) {
      DecorationDisposition& entry = m_decorations[*pCur->ti];

      // Decide what to do with this entry:
      switch(pCur->subscriberType) {
      case inTypeInvalid:
        // Should never happen--trivially ignore this entry
        break;
      case inTypeRequired:
        entry.m_subscribers.push_back(std::make_pair(&satCounter, true));
        break;
      case inTypeOptional:
        entry.m_subscribers.push_back(std::make_pair(&satCounter, false));
        break;
      case outTypeRef:
      case outTypeRefAutoReady:
        if(entry.m_publisher)
          throw autowiring_error("Added two publishers of the same decoration to the same factory");
        entry.m_publisher = &satCounter;
        break;
      }
    }
  }

  // Record divide between subscribers & recipients
  m_subscriberNum = m_satCounters.size();

  Reset();
}

// This must appear in .cpp in order to avoid compilation failure due to:
// "Arithmetic on a point to an incomplete type 'SatCounter'"
AutoPacket::~AutoPacket() {}

ObjectPool<AutoPacket> AutoPacket::CreateObjectPool(AutoPacketFactory& factory, const std::shared_ptr<Object>& outstanding) {
  return ObjectPool<AutoPacket>(
    ~0,
    ~0,
    [&factory, &outstanding] { return new AutoPacket(factory, outstanding); },
    [] (AutoPacket& packet) { packet.Initialize(); },
    [] (AutoPacket& packet) { packet.Finalize(); }
  );
}

void AutoPacket::MarkUnsatisfiable(const std::type_info& info) {
  std::list<SatCounter*> callQueue;
  DecorationDisposition* decoration;
  {
    std::lock_guard<std::mutex> lk(m_lock);
    auto dFind = m_decorations.find(info);
    if(dFind == m_decorations.end())
      // Trivial return, there's no subscriber to this decoration and so we have nothing to do
      return;

    // Update satisfaction inside of lock
    decoration = &dFind->second;
    for(const auto& satCounter : decoration->m_subscribers) {
      if(satCounter.second)
        // Entry is mandatory, leave it unsatisfaible
        continue;

      // Entry is optional, we will call if we're satisfied after decrementing this optional field
      if(satCounter.first->Decrement(false))
        callQueue.push_back(satCounter.first);
    }
  }

  // Make calls outside of lock, to avoid deadlock from decorations in methods
  for (SatCounter* call : callQueue)
    call->CallAutoFilter(*this);
}

void AutoPacket::UpdateSatisfaction(const std::type_info& info) {
  std::list<SatCounter*> callQueue;
  DecorationDisposition* decoration;
  {
    std::lock_guard<std::mutex> lk(m_lock);
    auto dFind = m_decorations.find(info);
    if(dFind == m_decorations.end())
      // Trivial return, there's no subscriber to this decoration and so we have nothing to do
      return;

    // Update satisfaction inside of lock
    decoration = &dFind->second;
    for(const auto& satCounter : decoration->m_subscribers)
      if(satCounter.first->Decrement(satCounter.second))
        callQueue.push_back(satCounter.first);
  }

  // Make calls outside of lock, to avoid deadlock from decorations in methods
  for (SatCounter* call : callQueue)
    call->CallAutoFilter(*this);
}

void AutoPacket::PulseSatisfaction(DecorationDisposition* pTypeSubs[], size_t nInfos) {
  std::list<SatCounter*> callQueue;
  // First pass, decrement what we can:
  {
    std::lock_guard<std::mutex> lk(m_lock);
    for(size_t i = nInfos; i--;) {
      for(std::pair<SatCounter*, bool>& subscriber : pTypeSubs[i]->m_subscribers) {
        SatCounter* cur = subscriber.first;
        if(
          // We only care about mandatory inputs
          subscriber.second &&

          // We only care about sat counters that aren't deferred--skip everyone else
          // Deferred calls will be too late.
          !cur->IsDeferred() &&

          // Now do the decrementation, and only proceed if the decremented value is zero
          !--cur->remaining
        )
          // Finally, queue a call for this type
          callQueue.push_back(cur);
      }
    }
  }

  // Make calls outside of lock, to avoid deadlock from decorations in methods
  for (SatCounter* call : callQueue)
    call->CallAutoFilter(*this);

  // Reset all counters
  // since data in this call will not be available subsequently
  {
    std::lock_guard<std::mutex> lk(m_lock);
    for(size_t i = nInfos; i--;) {
      for(const auto& satCounter : pTypeSubs[i]->m_subscribers) {
        SatCounter* cur = satCounter.first;
        if (satCounter.second) {
          ++cur->remaining;
        }
      }
    }
  }
}

void AutoPacket::Reset(void) {
  // Initialize all counters:
  std::lock_guard<std::mutex> lk(m_lock);
  for(auto& satCounter : m_satCounters)
    satCounter.Reset();

  // Clear all references:
  for(auto& decoration : m_decorations)
    decoration.second.Reset();
}

void AutoPacket::Initialize(void) {
  // Hold an outstanding count from the parent packet factory
  m_outstanding = m_outstandingRemote;
  if(!m_outstanding)
    throw autowiring_error("Cannot proceed with this packet, enclosing context already expired");

  // Find all subscribers with no required or optional arguments:
  std::list<SatCounter*> callCounters;
  for (auto& satCounter : m_satCounters)
    if (satCounter)
      callCounters.push_back(&satCounter);

  // Call all subscribers with no required or optional arguments:
  // NOTE: This may result in decorations that cause other subscribers to be called.
  for (SatCounter* call : callCounters)
    call->CallAutoFilter(*this);

  // Initial satisfaction of the AutoPacket:
  UpdateSatisfaction(typeid(AutoPacket));
}

void AutoPacket::Finalize(void) {
  // Queue calls to ensure that calls to Decorate inside of AutoFilter methods
  // will NOT effect the resolution of optional arguments.
  std::list<SatCounter*> callQueue;
  {
    std::lock_guard<std::mutex> lk(m_lock);
    for(auto& decoration : m_decorations)
      for(auto& satCounter : decoration.second.m_subscribers)
        if(!satCounter.second)
          if(satCounter.first->Resolve())
            callQueue.push_back(satCounter.first);
  }
  for (SatCounter* call : callQueue)
    call->CallAutoFilter(*this);

  // Remove all recipients & clean up the decorations list
  // ASSERT: This reverse the order of accumulation,
  // so searching for the subscriber is avoided.
  while (m_satCounters.size() > m_subscriberNum) {
    SatCounter& recipient = m_satCounters.back();

    for(auto pCur = recipient.GetAutoFilterInput();
        *pCur;
        pCur++
        ) {
      DecorationDisposition& entry = m_decorations[*pCur->ti];
      switch(pCur->subscriberType) {
        case inTypeInvalid:
          // Should never happen--trivially ignore this entry
          break;
        case inTypeRequired:
          assert(entry.m_subscribers.size() > 0);
          assert(&recipient == entry.m_subscribers.back().first);
          entry.m_subscribers.pop_back();
          break;
        case inTypeOptional:
          assert(entry.m_subscribers.size() > 0);
          assert(&recipient == entry.m_subscribers.back().first);
          entry.m_subscribers.pop_back();
          break;
        case outTypeRef:
        case outTypeRefAutoReady:
          assert(&recipient == entry.m_publisher);
          entry.m_publisher = nullptr;
          break;
      }
    }

    m_satCounters.pop_back();
  }

  // Remove decoration dispositions specific to subscribers
  t_decorationMap::iterator dItr = m_decorations.begin();
  t_decorationMap::iterator dEnd = m_decorations.end();
  while (dItr != dEnd) {
    if (dItr->second.m_subscribers.empty())
      dItr = m_decorations.erase(dItr);
    else
      ++dItr;
  }

  Reset();
}

void AutoPacket::InitializeRecipient(const AutoFilterDescriptor& descriptor) {
  SatCounter* call = nullptr;
  {
    std::lock_guard<std::mutex> lk(m_lock);

    // (1) Append & Initialize new satisfaction counter
    m_satCounters.push_back(descriptor);
    SatCounter& recipient = m_satCounters.back();
    recipient.Reset();

    // (2) Update satisfaction & Append types from subscriber
    for(auto pCur = recipient.GetAutoFilterInput();
        *pCur;
        pCur++
        ) {
      DecorationDisposition& entry = m_decorations[*pCur->ti];
      switch(pCur->subscriberType) {
        case inTypeInvalid:
          // Should never happen--trivially ignore this entry
          break;
        case inTypeRequired:
          entry.m_subscribers.push_back(std::make_pair(&recipient, true));
          if (entry.satisfied)
            recipient.Decrement(true);
          break;
        case inTypeOptional:
          entry.m_subscribers.push_back(std::make_pair(&recipient, false));
          if (entry.satisfied)
            recipient.Decrement(false);
          break;
        case outTypeRef:
        case outTypeRefAutoReady:
          if(entry.m_publisher)
            throw autowiring_error("Added two publishers of the same decoration to the same factory");
          entry.m_publisher = &recipient;
          break;
      }
    }

    // (3) Check call status inside of lock
    if (recipient) {
      call = &recipient;
    }
  }

  // (3) If all types are satisfied, call AutoFilter now.
  if (call)
    call->CallAutoFilter(*this);
}

bool AutoPacket::HasSubscribers(const std::type_info& ti) const {
  std::lock_guard<std::mutex> lk(m_lock);
  return m_decorations.count(ti) != 0;
}

// Copyright (C) 2012-2014 Leap Motion, Inc. All rights reserved.
#pragma once
#include TYPE_INDEX_HEADER

class Object;

// Checks if an Object* listens to a event T;
struct TypeIdentifierBase {
  virtual bool IsSameAs(const Object* obj) = 0;
  virtual const std::type_info& Type() = 0;
};

template<typename T>
  struct TypeIdentifier:
public TypeIdentifierBase
{
  // true if "obj" is an event receiver for T
  bool IsSameAs(const Object* obj) override {
    return !!dynamic_cast<const T*>(obj);
  }
  
  const std::type_info& Type() override {
    return typeid(T);
  }
};

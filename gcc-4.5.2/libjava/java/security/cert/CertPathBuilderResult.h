
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_security_cert_CertPathBuilderResult__
#define __java_security_cert_CertPathBuilderResult__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace java
  {
    namespace security
    {
      namespace cert
      {
          class CertPath;
          class CertPathBuilderResult;
      }
    }
  }
}

class java::security::cert::CertPathBuilderResult : public ::java::lang::Object
{

public:
  virtual ::java::lang::Object * clone() = 0;
  virtual ::java::security::cert::CertPath * getCertPath() = 0;
  static ::java::lang::Class class$;
} __attribute__ ((java_interface));

#endif // __java_security_cert_CertPathBuilderResult__

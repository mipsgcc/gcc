
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_JTextField$AccessibleJTextField__
#define __javax_swing_JTextField$AccessibleJTextField__

#pragma interface

#include <javax/swing/text/JTextComponent$AccessibleJTextComponent.h>
extern "Java"
{
  namespace javax
  {
    namespace accessibility
    {
        class AccessibleStateSet;
    }
    namespace swing
    {
        class JTextField;
        class JTextField$AccessibleJTextField;
    }
  }
}

class javax::swing::JTextField$AccessibleJTextField : public ::javax::swing::text::JTextComponent$AccessibleJTextComponent
{

public: // actually protected
  JTextField$AccessibleJTextField(::javax::swing::JTextField *);
public:
  virtual ::javax::accessibility::AccessibleStateSet * getAccessibleStateSet();
private:
  static const jlong serialVersionUID = 8255147276740453036LL;
public: // actually package-private
  ::javax::swing::JTextField * __attribute__((aligned(__alignof__( ::javax::swing::text::JTextComponent$AccessibleJTextComponent)))) this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_JTextField$AccessibleJTextField__

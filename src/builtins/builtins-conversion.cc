// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

class ConversionBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit ConversionBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void Generate_NonPrimitiveToPrimitive(ToPrimitiveHint hint);

  void Generate_OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint);
};

Handle<Code> Builtins::NonPrimitiveToPrimitive(ToPrimitiveHint hint) {
  switch (hint) {
    case ToPrimitiveHint::kDefault:
      return NonPrimitiveToPrimitive_Default();
    case ToPrimitiveHint::kNumber:
      return NonPrimitiveToPrimitive_Number();
    case ToPrimitiveHint::kString:
      return NonPrimitiveToPrimitive_String();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

// ES6 section 7.1.1 ToPrimitive ( input [ , PreferredType ] )
void ConversionBuiltinsAssembler::Generate_NonPrimitiveToPrimitive(
    ToPrimitiveHint hint) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  // Lookup the @@toPrimitive property on the {input}.
  Callable callable = CodeFactory::GetProperty(isolate());
  Node* to_primitive_symbol = HeapConstant(factory()->to_primitive_symbol());
  Node* exotic_to_prim =
      CallStub(callable, context, input, to_primitive_symbol);

  // Check if {exotic_to_prim} is neither null nor undefined.
  Label ordinary_to_primitive(this);
  GotoIf(WordEqual(exotic_to_prim, NullConstant()), &ordinary_to_primitive);
  GotoIf(WordEqual(exotic_to_prim, UndefinedConstant()),
         &ordinary_to_primitive);
  {
    // Invoke the {exotic_to_prim} method on the {input} with a string
    // representation of the {hint}.
    Callable callable =
        CodeFactory::Call(isolate(), ConvertReceiverMode::kNotNullOrUndefined);
    Node* hint_string = HeapConstant(factory()->ToPrimitiveHintString(hint));
    Node* result =
        CallJS(callable, context, exotic_to_prim, input, hint_string);

    // Verify that the {result} is actually a primitive.
    Label if_resultisprimitive(this),
        if_resultisnotprimitive(this, Label::kDeferred);
    GotoIf(TaggedIsSmi(result), &if_resultisprimitive);
    Node* result_instance_type = LoadInstanceType(result);
    STATIC_ASSERT(FIRST_PRIMITIVE_TYPE == FIRST_TYPE);
    Branch(Int32LessThanOrEqual(result_instance_type,
                                Int32Constant(LAST_PRIMITIVE_TYPE)),
           &if_resultisprimitive, &if_resultisnotprimitive);

    Bind(&if_resultisprimitive);
    {
      // Just return the {result}.
      Return(result);
    }

    Bind(&if_resultisnotprimitive);
    {
      // Somehow the @@toPrimitive method on {input} didn't yield a primitive.
      TailCallRuntime(Runtime::kThrowCannotConvertToPrimitive, context);
    }
  }

  // Convert using the OrdinaryToPrimitive algorithm instead.
  Bind(&ordinary_to_primitive);
  {
    Callable callable = CodeFactory::OrdinaryToPrimitive(
        isolate(), (hint == ToPrimitiveHint::kString)
                       ? OrdinaryToPrimitiveHint::kString
                       : OrdinaryToPrimitiveHint::kNumber);
    TailCallStub(callable, context, input);
  }
}

TF_BUILTIN(NonPrimitiveToPrimitive_Default, ConversionBuiltinsAssembler) {
  Generate_NonPrimitiveToPrimitive(ToPrimitiveHint::kDefault);
}

TF_BUILTIN(NonPrimitiveToPrimitive_Number, ConversionBuiltinsAssembler) {
  Generate_NonPrimitiveToPrimitive(ToPrimitiveHint::kNumber);
}

TF_BUILTIN(NonPrimitiveToPrimitive_String, ConversionBuiltinsAssembler) {
  Generate_NonPrimitiveToPrimitive(ToPrimitiveHint::kString);
}

TF_BUILTIN(StringToNumber, CodeStubAssembler) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Return(StringToNumber(context, input));
}

TF_BUILTIN(ToName, CodeStubAssembler) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Return(ToName(context, input));
}

TF_BUILTIN(NonNumberToNumber, CodeStubAssembler) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Return(NonNumberToNumber(context, input));
}

// ES6 section 7.1.3 ToNumber ( argument )
TF_BUILTIN(ToNumber, CodeStubAssembler) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Return(ToNumber(context, input));
}

TF_BUILTIN(ToString, CodeStubAssembler) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Label is_number(this);
  Label runtime(this);

  GotoIf(TaggedIsSmi(input), &is_number);

  Node* input_map = LoadMap(input);
  Node* input_instance_type = LoadMapInstanceType(input_map);

  Label not_string(this);
  GotoIfNot(IsStringInstanceType(input_instance_type), &not_string);
  Return(input);

  Label not_heap_number(this);

  Bind(&not_string);
  { Branch(IsHeapNumberMap(input_map), &is_number, &not_heap_number); }

  Bind(&is_number);
  { Return(NumberToString(context, input)); }

  Bind(&not_heap_number);
  {
    GotoIf(Word32NotEqual(input_instance_type, Int32Constant(ODDBALL_TYPE)),
           &runtime);
    Return(LoadObjectField(input, Oddball::kToStringOffset));
  }

  Bind(&runtime);
  { Return(CallRuntime(Runtime::kToString, context, input)); }
}

Handle<Code> Builtins::OrdinaryToPrimitive(OrdinaryToPrimitiveHint hint) {
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      return OrdinaryToPrimitive_Number();
    case OrdinaryToPrimitiveHint::kString:
      return OrdinaryToPrimitive_String();
  }
  UNREACHABLE();
  return Handle<Code>::null();
}

// 7.1.1.1 OrdinaryToPrimitive ( O, hint )
void ConversionBuiltinsAssembler::Generate_OrdinaryToPrimitive(
    OrdinaryToPrimitiveHint hint) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Variable var_result(this, MachineRepresentation::kTagged);
  Label return_result(this, &var_result);

  Handle<String> method_names[2];
  switch (hint) {
    case OrdinaryToPrimitiveHint::kNumber:
      method_names[0] = factory()->valueOf_string();
      method_names[1] = factory()->toString_string();
      break;
    case OrdinaryToPrimitiveHint::kString:
      method_names[0] = factory()->toString_string();
      method_names[1] = factory()->valueOf_string();
      break;
  }
  for (Handle<String> name : method_names) {
    // Lookup the {name} on the {input}.
    Callable callable = CodeFactory::GetProperty(isolate());
    Node* name_string = HeapConstant(name);
    Node* method = CallStub(callable, context, input, name_string);

    // Check if the {method} is callable.
    Label if_methodiscallable(this),
        if_methodisnotcallable(this, Label::kDeferred);
    GotoIf(TaggedIsSmi(method), &if_methodisnotcallable);
    Node* method_map = LoadMap(method);
    Branch(IsCallableMap(method_map), &if_methodiscallable,
           &if_methodisnotcallable);

    Bind(&if_methodiscallable);
    {
      // Call the {method} on the {input}.
      Callable callable = CodeFactory::Call(
          isolate(), ConvertReceiverMode::kNotNullOrUndefined);
      Node* result = CallJS(callable, context, method, input);
      var_result.Bind(result);

      // Return the {result} if it is a primitive.
      GotoIf(TaggedIsSmi(result), &return_result);
      Node* result_instance_type = LoadInstanceType(result);
      STATIC_ASSERT(FIRST_PRIMITIVE_TYPE == FIRST_TYPE);
      GotoIf(Int32LessThanOrEqual(result_instance_type,
                                  Int32Constant(LAST_PRIMITIVE_TYPE)),
             &return_result);
    }

    // Just continue with the next {name} if the {method} is not callable.
    Goto(&if_methodisnotcallable);
    Bind(&if_methodisnotcallable);
  }

  TailCallRuntime(Runtime::kThrowCannotConvertToPrimitive, context);

  Bind(&return_result);
  Return(var_result.value());
}

TF_BUILTIN(OrdinaryToPrimitive_Number, ConversionBuiltinsAssembler) {
  Generate_OrdinaryToPrimitive(OrdinaryToPrimitiveHint::kNumber);
}

TF_BUILTIN(OrdinaryToPrimitive_String, ConversionBuiltinsAssembler) {
  Generate_OrdinaryToPrimitive(OrdinaryToPrimitiveHint::kString);
}

// ES6 section 7.1.2 ToBoolean ( argument )
TF_BUILTIN(ToBoolean, CodeStubAssembler) {
  Node* value = Parameter(TypeConversionDescriptor::kArgument);

  Label return_true(this), return_false(this);
  BranchIfToBooleanIsTrue(value, &return_true, &return_false);

  Bind(&return_true);
  Return(BooleanConstant(true));

  Bind(&return_false);
  Return(BooleanConstant(false));
}

TF_BUILTIN(ToLength, CodeStubAssembler) {
  Node* context = Parameter(1);

  // We might need to loop once for ToNumber conversion.
  Variable var_len(this, MachineRepresentation::kTagged, Parameter(0));
  Label loop(this, &var_len);
  Goto(&loop);
  Bind(&loop);
  {
    // Shared entry points.
    Label return_len(this), return_two53minus1(this, Label::kDeferred),
        return_zero(this, Label::kDeferred);

    // Load the current {len} value.
    Node* len = var_len.value();

    // Check if {len} is a positive Smi.
    GotoIf(TaggedIsPositiveSmi(len), &return_len);

    // Check if {len} is a (negative) Smi.
    GotoIf(TaggedIsSmi(len), &return_zero);

    // Check if {len} is a HeapNumber.
    Label if_lenisheapnumber(this),
        if_lenisnotheapnumber(this, Label::kDeferred);
    Branch(IsHeapNumberMap(LoadMap(len)), &if_lenisheapnumber,
           &if_lenisnotheapnumber);

    Bind(&if_lenisheapnumber);
    {
      // Load the floating-point value of {len}.
      Node* len_value = LoadHeapNumberValue(len);

      // Check if {len} is not greater than zero.
      GotoIfNot(Float64GreaterThan(len_value, Float64Constant(0.0)),
                &return_zero);

      // Check if {len} is greater than or equal to 2^53-1.
      GotoIf(Float64GreaterThanOrEqual(len_value,
                                       Float64Constant(kMaxSafeInteger)),
             &return_two53minus1);

      // Round the {len} towards -Infinity.
      Node* value = Float64Floor(len_value);
      Node* result = ChangeFloat64ToTagged(value);
      Return(result);
    }

    Bind(&if_lenisnotheapnumber);
    {
      // Need to convert {len} to a Number first.
      Callable callable = CodeFactory::NonNumberToNumber(isolate());
      var_len.Bind(CallStub(callable, context, len));
      Goto(&loop);
    }

    Bind(&return_len);
    Return(var_len.value());

    Bind(&return_two53minus1);
    Return(NumberConstant(kMaxSafeInteger));

    Bind(&return_zero);
    Return(SmiConstant(Smi::kZero));
  }
}

TF_BUILTIN(ToInteger, CodeStubAssembler) {
  Node* input = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Return(ToInteger(context, input));
}

// ES6 section 7.1.13 ToObject (argument)
TF_BUILTIN(ToObject, CodeStubAssembler) {
  Label if_number(this, Label::kDeferred), if_notsmi(this), if_jsreceiver(this),
      if_noconstructor(this, Label::kDeferred), if_wrapjsvalue(this);

  Node* object = Parameter(TypeConversionDescriptor::kArgument);
  Node* context = Parameter(TypeConversionDescriptor::kContext);

  Variable constructor_function_index_var(this,
                                          MachineType::PointerRepresentation());

  Branch(TaggedIsSmi(object), &if_number, &if_notsmi);

  Bind(&if_notsmi);
  Node* map = LoadMap(object);

  GotoIf(IsHeapNumberMap(map), &if_number);

  Node* instance_type = LoadMapInstanceType(map);
  GotoIf(IsJSReceiverInstanceType(instance_type), &if_jsreceiver);

  Node* constructor_function_index = LoadMapConstructorFunctionIndex(map);
  GotoIf(WordEqual(constructor_function_index,
                   IntPtrConstant(Map::kNoConstructorFunctionIndex)),
         &if_noconstructor);
  constructor_function_index_var.Bind(constructor_function_index);
  Goto(&if_wrapjsvalue);

  Bind(&if_number);
  constructor_function_index_var.Bind(
      IntPtrConstant(Context::NUMBER_FUNCTION_INDEX));
  Goto(&if_wrapjsvalue);

  Bind(&if_wrapjsvalue);
  Node* native_context = LoadNativeContext(context);
  Node* constructor = LoadFixedArrayElement(
      native_context, constructor_function_index_var.value());
  Node* initial_map =
      LoadObjectField(constructor, JSFunction::kPrototypeOrInitialMapOffset);
  Node* js_value = Allocate(JSValue::kSize);
  StoreMapNoWriteBarrier(js_value, initial_map);
  StoreObjectFieldRoot(js_value, JSValue::kPropertiesOffset,
                       Heap::kEmptyFixedArrayRootIndex);
  StoreObjectFieldRoot(js_value, JSObject::kElementsOffset,
                       Heap::kEmptyFixedArrayRootIndex);
  StoreObjectField(js_value, JSValue::kValueOffset, object);
  Return(js_value);

  Bind(&if_noconstructor);
  TailCallRuntime(
      Runtime::kThrowUndefinedOrNullToObject, context,
      HeapConstant(factory()->NewStringFromAsciiChecked("ToObject", TENURED)));

  Bind(&if_jsreceiver);
  Return(object);
}

// Deprecated ES5 [[Class]] internal property (used to implement %_ClassOf).
TF_BUILTIN(ClassOf, CodeStubAssembler) {
  Node* object = Parameter(TypeofDescriptor::kObject);

  Return(ClassOf(object));
}

// ES6 section 12.5.5 typeof operator
TF_BUILTIN(Typeof, CodeStubAssembler) {
  Node* object = Parameter(TypeofDescriptor::kObject);
  Node* context = Parameter(TypeofDescriptor::kContext);

  Return(Typeof(object, context));
}

}  // namespace internal
}  // namespace v8

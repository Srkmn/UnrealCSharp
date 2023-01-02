﻿#include "Domain/InternalCall/FPropertyImplementation.h"
#include "Environment/FCSharpEnvironment.h"

PRIMITIVE_PROPERTY_IMPLEMENTATION(Byte, uint8)

PRIMITIVE_PROPERTY_IMPLEMENTATION(UInt16, uint16)

PRIMITIVE_PROPERTY_IMPLEMENTATION(UInt32, uint32)

PRIMITIVE_PROPERTY_IMPLEMENTATION(UInt64, uint64)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Int8, int8)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Int16, int16)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Int, int32)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Int64, int64)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Bool, bool)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Float, float)

COMPOUND_PROPERTY_IMPLEMENTATION(Object, MonoObject)

COMPOUND_PROPERTY_IMPLEMENTATION(Class, MonoObject)

COMPOUND_PROPERTY_IMPLEMENTATION(Interface, MonoObject)

COMPOUND_PROPERTY_IMPLEMENTATION(Array, MonoObject)

COMPOUND_PROPERTY_IMPLEMENTATION(WeakObject, MonoObject)

PRIMITIVE_PROPERTY_IMPLEMENTATION(Double, double)

COMPOUND_PROPERTY_IMPLEMENTATION(Map, MonoObject)

COMPOUND_PROPERTY_IMPLEMENTATION(Set, MonoObject)

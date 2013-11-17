
#include "Primitive.h"

#include <primitivesCore/Routine.h>

#include <vmobjects/VMClass.h>
#include <vmobjects/VMMethod.h>

_Primitive::_Primitive() : PrimitiveContainer() {
    SetPrimitive("signature", new Routine<_Primitive>(this, &_Primitive::Signature));
    SetPrimitive("holder",    new Routine<_Primitive>(this, &_Primitive::Holder));
}

void _Primitive::Holder(pVMObject, pVMFrame frame) {
    pVMMethod self = static_cast<pVMMethod>(frame->Pop());
    frame->Push(self->GetHolder());
}

void _Primitive::Signature(pVMObject, pVMFrame frame) {
    pVMMethod self = static_cast<pVMMethod>(frame->Pop());
    frame->Push(self->GetSignature());
}
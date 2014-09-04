/*
 *
 *
 Copyright (c) 2007 Michael Haupt, Tobias Pape, Arne Bergmann
 Software Architecture Group, Hasso Plattner Institute, Potsdam, Germany
 http://www.hpi.uni-potsdam.de/swa/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include "ClassGenerationContext.h"

#include "../vmobjects/VMSymbol.h"
#include "../vmobjects/VMObject.h"
#include "../vmobjects/VMClass.h"

ClassGenerationContext::ClassGenerationContext() :
        instanceFields(), instanceMethods(), classFields(), classMethods() {
    name = nullptr;
    superName = nullptr;
    classSide = false;
}

ClassGenerationContext::~ClassGenerationContext() {
}

void ClassGenerationContext::AddClassField(pVMSymbol field) {
    classFields.Add(field);
}

void ClassGenerationContext::AddInstanceField(pVMSymbol field) {
    instanceFields.Add(field);
}

void ClassGenerationContext::SetInstanceFieldsOfSuper(pVMArray fields) {
    long num = fields->GetNumberOfIndexableFields();
    for (long i = 0; i < num; i ++) {
        pVMSymbol fieldName = (pVMSymbol)fields->GetIndexableField(i);
        instanceFields.Add(fieldName);
    }
}

void ClassGenerationContext::SetClassFieldsOfSuper(pVMArray fields) {
    long num = fields->GetNumberOfIndexableFields();
    for (long i = 0; i < num; i ++) {
        pVMSymbol fieldName = (pVMSymbol)fields->GetIndexableField(i);
        classFields.Add(fieldName);
    }
}

bool ClassGenerationContext::HasField(const StdString& field) {
    if (IsClassSide()) {
        return classFields.IndexOf(GetUniverse()->SymbolFor(field)) != -1;
    } else {
        return instanceFields.IndexOf(GetUniverse()->SymbolFor(field)) != -1;
    }
}

int16_t ClassGenerationContext::GetFieldIndex(pVMSymbol field) {
    if (IsClassSide()) {
        return classFields.IndexOf(field);
    } else {
        return instanceFields.IndexOf(field);
    }
}

void ClassGenerationContext::AddInstanceMethod(pVMObject method) {
    instanceMethods.Add(method);
}

void ClassGenerationContext::AddClassMethod(pVMObject method) {
    classMethods.Add(method);
}

pVMClass ClassGenerationContext::Assemble() {
    // build class class name
    StdString ccname = string(name->GetStdString()) + " class";

    // Load the super class
    pVMClass superClass = GetUniverse()->LoadClass(superName);

    // Allocate the class of the resulting class
    pVMClass resultClass = GetUniverse()->NewClass(metaClassClass);

    // Initialize the class of the resulting class
    resultClass->SetInstanceFields(GetUniverse()->NewArrayList(classFields));
    resultClass->SetInstanceInvokables(GetUniverse()->NewArrayList(classMethods));
    resultClass->SetName(GetUniverse()->SymbolFor(ccname));

    pVMClass superMClass = superClass->GetClass();
    resultClass->SetSuperClass(superMClass);

    // Allocate the resulting class
    pVMClass result = GetUniverse()->NewClass(resultClass);

    // Initialize the resulting class
    result->SetInstanceFields(GetUniverse()->NewArrayList(instanceFields));
    result->SetInstanceInvokables(GetUniverse()->NewArrayList(instanceMethods));
    result->SetName(name);
    result->SetSuperClass(superClass);

    return result;
}

void ClassGenerationContext::AssembleSystemClass( pVMClass systemClass ) {
    systemClass->SetInstanceInvokables(GetUniverse()->NewArrayList
    (instanceMethods));
    systemClass->SetInstanceFields(GetUniverse()->NewArrayList(instanceFields));
    // class-bound == class-instance-bound 
        pVMClass superMClass = systemClass->GetClass();
        superMClass->SetInstanceInvokables(GetUniverse()->NewArrayList(classMethods));
        superMClass->SetInstanceFields(GetUniverse()->NewArrayList(classFields));
    }


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


#include <sstream> 
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <iomanip>

#include "Universe.h"
#include "Shell.h"

#include "../vmobjects/VMSymbol.h"
#include "../vmobjects/VMObject.h"
#include "../vmobjects/VMMethod.h"
#include "../vmobjects/VMClass.h"
#include "../vmobjects/VMFrame.h"
#include "../vmobjects/VMArray.h"
#include "../vmobjects/VMBlock.h"
#include "../vmobjects/VMDouble.h"
#include "../vmobjects/VMInteger.h"
#include "../vmobjects/VMString.h"
#include "../vmobjects/VMBigInteger.h"
#include "../vmobjects/VMEvaluationPrimitive.h"
#include "../interpreter/bytecodes.h"
#include "../compiler/Disassembler.h"
#include "../compiler/SourcecodeCompiler.h"
#ifdef USE_TAGGING
#include "../vmobjects/IntegerBox.h"
#endif

#ifdef CACHE_INTEGER
#ifndef INT_CACHE_MIN_VALUE
#define INT_CACHE_MIN_VALUE (-5)
#endif
#ifndef INT_CACHE_MAX_VALUE
#define INT_CACHE_MAX_VALUE (100)
#endif
pVMInteger prebuildInts[INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE + 1];
#endif

#define INT_HIST_SIZE 1

// Here we go:

short dumpBytecodes;
short gcVerbosity;


Universe* Universe::theUniverse = NULL;

VMObject* nilObject;
VMObject* trueObject;
VMObject* falseObject;
     
pVMClass objectClass;
pVMClass classClass;
pVMClass metaClassClass;
  
pVMClass nilClass;
pVMClass integerClass;
pVMClass bigIntegerClass;
pVMClass arrayClass;
pVMClass methodClass;
pVMClass symbolClass;
pVMClass frameClass;
pVMClass primitiveClass;
pVMClass stringClass;
pVMClass systemClass;
pVMClass blockClass;
pVMClass doubleClass;

pVMSymbol symbolIfTrue;
pVMSymbol symbolIfFalse;


std::map<std::string, pVMSymbol> symbolsMap;

std::string bm_name;
#ifdef GENERATE_ALLOCATION_STATISTICS
struct alloc_data {long noObjects; long sizeObjects;};
std::map<std::string, struct alloc_data> allocationStats;
#define LOG_ALLOCATION(TYPE,SIZE) {struct alloc_data tmp=allocationStats[TYPE];tmp.noObjects++;tmp.sizeObjects+=(SIZE);allocationStats[TYPE]=tmp;}
#endif

map<long, long> integerHist;

//Singleton accessor
Universe* Universe::GetUniverse() {
    if (!theUniverse) {
        ErrorExit("Trying to access uninitialized Universe, exiting.");
    }
	return theUniverse;
}


Universe* Universe::operator->() {
    if (!theUniverse) {
        ErrorExit("Trying to access uninitialized Universe, exiting.");
    }
	return theUniverse;
}


void Universe::Start(long argc, char** argv) {
	theUniverse = new Universe();
    theUniverse->initialize(argc, argv);
}


void Universe::Quit(long err) {
  cout << "Time spent in GC: [" << Timer::GCTimer->GetTotalTime() << "] msec" << endl;
#ifdef GENERATE_INTEGER_HISTOGRAM
  std::string file_name_hist = std::string(bm_name);
  file_name_hist.append("_integer_histogram.csv");
  fstream hist_csv(file_name_hist.c_str(), ios::out);

  for (map<long, long>::iterator it = integerHist.begin(); it != integerHist.end(); it++) {
    hist_csv << it->first << ", " << it->second << endl;
  }
#endif

#ifdef LOG_RECEIVER_TYPES
  std::string file_name_receivers = std::string(bm_name);
  file_name_receivers.append("_receivers.csv");
  fstream receivers(file_name_receivers.c_str(), ios::out);
  for (map<StdString, long>::iterator it = theUniverse->receiverTypes.begin(); it != theUniverse->receiverTypes.end(); it++)
    receivers << it->first << ",  " << it->second << endl;

  std::string file_name_send_types = std::string(bm_name);
  file_name_send_types.append("_send_types.csv");
  fstream send_stat(file_name_send_types.c_str(), ios::out);
  send_stat << "#name, percentage_primitive_calls, no_primitive_calls, no_non_primitive_calls" << endl;
  for (map<StdString, Universe::stat_data>::iterator it = theUniverse->callStats.begin(); it != theUniverse->callStats.end(); it++)
    send_stat << it->first << ", " << setiosflags(ios::fixed) << setprecision(2) << (double)(it->second.noPrimitiveCalls) / (double)(it->second.noCalls) <<
        ", " << it->second.noPrimitiveCalls << ", " << it->second.noCalls - it->second.noPrimitiveCalls << endl;
#endif

#ifdef GENERATE_ALLOCATION_STATISTICS
  std::string file_name_allocation = std::string(bm_name);
  file_name_allocation.append("_allocation_statistics.csv");

  fstream file_alloc_stats(file_name_allocation.c_str(), ios::out);
  map<std::string, struct alloc_data>::iterator iter;
  for (iter = allocationStats.begin(); iter != allocationStats.end(); iter++)
  {
    file_alloc_stats << iter->first << ", " << iter->second.noObjects << ", " << iter->second.sizeObjects << std::endl;
  }
#endif
  if (theUniverse) delete(theUniverse);
  exit(err);
}


void Universe::ErrorExit( const char* err) {
    cout << "Runtime error: " << err << endl;
    Quit(ERR_FAIL);
}

vector<StdString> Universe::handleArguments( long argc, char** argv ) {
  vector<StdString> vmArgs = vector<StdString>();
  dumpBytecodes = 0;
  gcVerbosity = 0;
  for (long i = 1; i < argc ; ++i) {

    if (strncmp(argv[i], "-cp", 3) == 0) {
      if ((argc == i + 1) || classPath.size() > 0)
        printUsageAndExit(argv[0]);
      setupClassPath(StdString(argv[++i]));
    } else if (strncmp(argv[i], "-d", 2) == 0) {
      ++dumpBytecodes;
    } else if (strncmp(argv[i], "-g", 2) == 0) {
      ++gcVerbosity;
    } else if (strncmp(argv[i], "-H",2) == 0) {
      long heap_size = 0;
      char* unit = (char*)malloc(3);
      if (sscanf(argv[i],"-H%ld%2s", &heap_size, unit) == 2) {
        if (strcmp(unit, "KB") == 0)
          heapSize = heap_size * 1024;
        else if (strcmp(unit, "MB") == 0)
          heapSize = heap_size * 1024 * 1024;

      }
      else
        printUsageAndExit(argv[0]);
      delete unit;

    } else if ((strncmp(argv[i], "-h", 2) == 0) ||
               (strncmp(argv[i], "--help", 6) == 0)) {
      printUsageAndExit(argv[0]);
    } else {
      vector<StdString> extPathTokens = vector<StdString>(2);
      StdString tmpString = StdString(argv[i]);
      if (this->getClassPathExt(extPathTokens, tmpString) ==
          ERR_SUCCESS) {
        this->addClassPath(extPathTokens[0]);
      }
      //Different from CSOM!!!:
      //In CSOM there is an else, where the original filename is pushed into the vm_args.
      //But unlike the class name in extPathTokens (extPathTokens[1]) that could
      //still have the .som suffix though.
      //So in SOM++ getClassPathExt will strip the suffix and add it to extPathTokens
      //even if there is no new class path present. So we can in any case do the following:
      vmArgs.push_back(extPathTokens[1]);
    }
  }
  addClassPath(StdString("."));

  return vmArgs;
}

long Universe::getClassPathExt(vector<StdString>& tokens,const StdString& arg ) const {
#define EXT_TOKENS 2
    long result = ERR_SUCCESS;
    long fpIndex = arg.find_last_of(fileSeparator);
    long ssepIndex = arg.find(".som");

    if (fpIndex == StdString::npos) { //no new path
        //different from CSOM (see also HandleArguments):
        //we still want to strip the suffix from the filename, so
        //we set the start to -1, in order to start the substring
        //from character 0. npos is -1 too, but this is to make sure
        fpIndex = -1;
        //instead of returning here directly, we have to remember that
        //there is no new class path and return it later
        result = ERR_FAIL;
    } else tokens[0] = arg.substr(0, fpIndex);
    
    //adding filename (minus ".som" if present) to second slot
    ssepIndex = ( (ssepIndex != StdString::npos) && (ssepIndex > fpIndex)) ?
                 (ssepIndex - 1) :
                 arg.length();
    tokens[1] = arg.substr(fpIndex + 1, ssepIndex - (fpIndex));
    return result;
}


long Universe::setupClassPath( const StdString& cp ) {
    try {
        std::stringstream ss ( cp );
        StdString token;

        long i = 0;
        while( getline(ss, token, pathSeparator) ) {
            classPath.push_back(token);
            ++i;
        }

        return ERR_SUCCESS;
    } catch(std::exception e){ 
        return ERR_FAIL;
    }
}


long Universe::addClassPath( const StdString& cp ) {
    classPath.push_back(cp);
    return ERR_SUCCESS;
}


void Universe::printUsageAndExit( char* executable ) const {
    cout << "Usage: " << executable << " [-options] [args...]" << endl << endl;
    cout << "where options include:" << endl;
    cout << "    -cp <directories separated by " << pathSeparator 
         << ">" << endl;
    cout << "        set search path for application classes" << endl;
    cout << "    -d  enable disassembling (twice for tracing)" << endl;
    cout << "    -g  enable garbage collection details:" << endl <<
                    "        1x - print statistics when VM shuts down" << endl <<
                    "        2x - print statistics upon each collection" << endl <<
                    "        3x - print statistics and dump _HEAP upon each "  << endl <<
                    "collection" << endl;
    cout << "    -HxMB set the _HEAP size to x MB (default: 1 MB)" << endl;
    cout << "    -HxKB set the _HEAP size to x KB (default: 1 MB)" << endl;
    cout << "    -h  show this help" << endl;

    Quit(ERR_SUCCESS);
}


Universe::Universe(){
	this->compiler = NULL;
	this->interpreter = NULL;
};


void Universe::initialize(long _argc, char** _argv) {
#ifdef GENERATE_ALLOCATION_STATISTICS
  allocationStats["VMArray"] = {0,0};
#endif

    heapSize = 1 * 1024 * 1024;

    vector<StdString> argv = this->handleArguments(_argc, _argv);
//remember file that was executed (for writing statistics)
    bm_name = argv[0];

    Heap::InitializeHeap(heapSize);

    heap = _HEAP;

    compiler = new SourcecodeCompiler();
    interpreter = new Interpreter();

#ifdef CACHE_INTEGER
  //create prebuilt integers
  for (long it = INT_CACHE_MIN_VALUE; it <= INT_CACHE_MAX_VALUE; ++it) {
    prebuildInts[(unsigned long)(it - INT_CACHE_MIN_VALUE)] = new (_HEAP) VMInteger(it);
  }
#endif
    
    InitializeGlobals();

    pVMObject systemObject = NewInstance(systemClass);

    this->SetGlobal(SymbolForChars("nil"), nilObject);
    this->SetGlobal(SymbolForChars("true"), trueObject);
    this->SetGlobal(SymbolForChars("false"), falseObject);
    this->SetGlobal(SymbolForChars("system"), systemObject);
    this->SetGlobal(SymbolForChars("System"), systemClass);
    this->SetGlobal(SymbolForChars("Block"), blockClass);
    symbolIfTrue = SymbolForChars("ifTrue:");
    symbolIfFalse = SymbolForChars("ifFalse:");
    
    pVMMethod bootstrapMethod = NewMethod(SymbolForChars("bootstrap"), 1, 0);
    bootstrapMethod->SetBytecode(0, BC_HALT);
    bootstrapMethod->SetNumberOfLocals(0);

    bootstrapMethod->SetMaximumNumberOfStackElements(2);
    bootstrapMethod->SetHolder(systemClass);
    
    if (argv.size() == 0) {
        Shell* shell = new Shell(bootstrapMethod);
        shell->Start();
        return;
    }

    /* only trace bootstrap if the number of cmd-line "-d"s is > 2 */
    short trace = 2 - dumpBytecodes;
    if(!(trace > 0)) dumpBytecodes = 1;

    pVMArray argumentsArray = _UNIVERSE->NewArrayFromArgv(argv);
    
    pVMFrame bootstrapFrame = interpreter->PushNewFrame(bootstrapMethod);
    bootstrapFrame->Push(systemObject);
    bootstrapFrame->Push(argumentsArray);

    pVMInvokable initialize = 
        static_cast<pVMInvokable>(systemClass->LookupInvokable(this->SymbolForChars("initialize:")));
    (*initialize)(bootstrapFrame);
    
    // reset "-d" indicator
    if(!(trace>0)) dumpBytecodes = 2 - trace;

    interpreter->Start();
}


Universe::~Universe() {
    if (interpreter) 
        delete(interpreter);
    if (compiler) 
        delete(compiler);

	// check done inside
    Heap::DestroyHeap();
}

void Universe::InitializeGlobals() {
    //
    //allocate nil object
    //
    nilObject = new (_HEAP) VMObject;
    nilObject->SetField(0, nilObject);

    metaClassClass = NewMetaclassClass();

    objectClass     = NewSystemClass();
    nilClass        = NewSystemClass();
    classClass      = NewSystemClass();
    arrayClass      = NewSystemClass();
    symbolClass     = NewSystemClass();
    methodClass     = NewSystemClass();
    integerClass    = NewSystemClass();
    bigIntegerClass = NewSystemClass();
    frameClass      = NewSystemClass();
    primitiveClass  = NewSystemClass();
    stringClass     = NewSystemClass();
    doubleClass     = NewSystemClass();
    
    nilObject->SetClass(nilClass);

    InitializeSystemClass(objectClass, NULL, "Object");
    InitializeSystemClass(classClass, objectClass, "Class");
    InitializeSystemClass(metaClassClass, classClass, "Metaclass");
    InitializeSystemClass(nilClass, objectClass, "Nil");
    InitializeSystemClass(arrayClass, objectClass, "Array");
    InitializeSystemClass(methodClass, arrayClass, "Method");
    InitializeSystemClass(symbolClass, objectClass, "Symbol");
    InitializeSystemClass(integerClass, objectClass, "Integer");
    InitializeSystemClass(bigIntegerClass, objectClass,
                                     "BigInteger");
    InitializeSystemClass(frameClass, arrayClass, "Frame");
    InitializeSystemClass(primitiveClass, objectClass,
                                     "Primitive");
    InitializeSystemClass(stringClass, objectClass, "String");
    InitializeSystemClass(doubleClass, objectClass, "Double");

    LoadSystemClass(objectClass);
    LoadSystemClass(classClass);
    LoadSystemClass(metaClassClass);
    LoadSystemClass(nilClass);
    LoadSystemClass(arrayClass);
    LoadSystemClass(methodClass);
    LoadSystemClass(symbolClass);
    LoadSystemClass(integerClass);
    LoadSystemClass(bigIntegerClass);
    LoadSystemClass(frameClass);
    LoadSystemClass(primitiveClass);
    LoadSystemClass(stringClass);
    LoadSystemClass(doubleClass);

    blockClass = LoadClass(_UNIVERSE->SymbolForChars("Block"));

    trueObject = (VMObject*)NewInstance(_UNIVERSE->LoadClass(_UNIVERSE->SymbolForChars("True")));
    falseObject = (VMObject*)NewInstance(_UNIVERSE->LoadClass(_UNIVERSE->SymbolForChars("False")));

    systemClass = LoadClass(_UNIVERSE->SymbolForChars("System"));
}

void Universe::Assert( bool value) const {
    if (!value)  {
        cout << "Assertion failed" << endl;
    }

}


pVMClass Universe::GetBlockClass() const {
    return blockClass;
}


pVMClass Universe::GetBlockClassWithArgs( long numberOfArguments) {
	map<long, pVMClass>::iterator it =
		blockClassesByNoOfArgs.find(numberOfArguments);
	if (it != blockClassesByNoOfArgs.end())
		return it->second;


    this->Assert(numberOfArguments < 10);
    ostringstream Str;
    Str << "Block" << numberOfArguments ;
    pVMSymbol name = SymbolFor(Str.str());
    pVMClass result = LoadClassBasic(name, NULL);
    result->AddInstancePrimitive(new (_HEAP) VMEvaluationPrimitive(numberOfArguments) );
    SetGlobal(name, result);

    blockClassesByNoOfArgs[numberOfArguments] = result;

    return result;
}



pVMObject Universe::GetGlobal( pVMSymbol name) {
    if (HasGlobal(name))
        return globals[name];
    return NULL;
}


bool Universe::HasGlobal( pVMSymbol name) {
    if (globals[name] != NULL) return true;
    else return false;
}


void Universe::InitializeSystemClass( pVMClass systemClass, 
                                     pVMClass superClass, const char* name) {
    StdString s_name(name);

    if (superClass != NULL) {
        systemClass->SetSuperClass(superClass);
        pVMClass sysClassClass = systemClass->GetClass();
        pVMClass superClassClass = superClass->GetClass();
        sysClassClass->SetSuperClass(superClassClass);
    } else {
        pVMClass sysClassClass = systemClass->GetClass();
        sysClassClass->SetSuperClass(classClass);
    }

    pVMClass sysClassClass = systemClass->GetClass();

    systemClass->SetInstanceFields(NewArray(0));
    sysClassClass->SetInstanceFields(NewArray(0));

    systemClass->SetInstanceInvokables(NewArray(0));
    sysClassClass->SetInstanceInvokables(NewArray(0));

    systemClass->SetName(SymbolFor(s_name));
    ostringstream Str;
    Str << s_name << " class";
    StdString classClassName(Str.str());
    sysClassClass->SetName(SymbolFor(classClassName));

    SetGlobal(systemClass->GetName(), systemClass);


}


pVMClass Universe::LoadClass( pVMSymbol name) {
   if (HasGlobal(name))
       return static_cast<pVMClass>(GetGlobal(name));

   pVMClass result = LoadClassBasic(name, NULL);

   if (!result) {
       cout << "can\'t load class " << name->GetStdString() << endl;
       Universe::Quit(ERR_FAIL);
   }

   if (result->HasPrimitives() || result->GetClass()->HasPrimitives())
       result->LoadPrimitives(classPath);
    
   return result;
}


pVMClass Universe::LoadClassBasic( pVMSymbol name, pVMClass systemClass) {
    StdString s_name = name->GetStdString();
    //cout << s_name.c_str() << endl;
    pVMClass result;

    for (vector<StdString>::iterator i = classPath.begin();
         i != classPath.end(); ++i) {
        result = compiler->CompileClass(*i, name->GetStdString(), systemClass);
        if (result) {
            if (dumpBytecodes) {
                Disassembler::Dump(result->GetClass());
                Disassembler::Dump(result);
            }
            return result;
        }

    }
    return NULL;
}


pVMClass Universe::LoadShellClass( StdString& stmt) {
    pVMClass result = compiler->CompileClassString(stmt, NULL);
     if(dumpBytecodes)
         Disassembler::Dump(result);
    return result;
}


void Universe::LoadSystemClass( pVMClass systemClass) {
    pVMClass result =
        LoadClassBasic(systemClass->GetName(), systemClass);
    StdString s = systemClass->GetName()->GetStdString();

    if (!result) {
        cout << "Can\'t load system class: " << s;
        Universe::Quit(ERR_FAIL);
    }

    if (result->HasPrimitives() || result->GetClass()->HasPrimitives())
        result->LoadPrimitives(classPath);
}


pVMArray Universe::NewArray( long size) const {
    long additionalBytes = size*sizeof(pVMObject);
#if GC_TYPE==GENERATIONAL
	//if the array is too big for the nursery, we will directly allocate a
	// mature object
	bool outsideNursery = 
		additionalBytes+sizeof(VMArray) > _HEAP->GetMaxNurseryObjectSize();

    pVMArray result = new (_HEAP, additionalBytes, outsideNursery) VMArray(size);
	if (outsideNursery)
		result->SetGCField(MASK_OBJECT_IS_OLD);
#else
    pVMArray result = new (_HEAP, additionalBytes) VMArray(size);
#endif
    result->SetClass(arrayClass);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMArray", result->GetObjectSize());
#endif
    return result;
}


pVMArray Universe::NewArrayFromArgv( const vector<StdString>& argv) const {
    pVMArray result = NewArray(argv.size());
    long j = 0;
    for (vector<StdString>::const_iterator i = argv.begin();
         i != argv.end(); ++i) {
        result->SetIndexableField(j, NewString(*i));
        ++j;
    }

    return result;
}


pVMArray Universe::NewArrayList(ExtendedList<pVMObject>& list ) const {
    long size = list.Size();
    pVMArray result = NewArray(size);

    if (result)  {
        for (long i = 0; i < size; ++i) {
            pVMObject elem = list.Get(i);
            result->SetIndexableField(i, elem);
        }
    }
    return result;
}


pVMBigInteger Universe::NewBigInteger( int64_t value) const {
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMBigInteger", sizeof(VMBigInteger));
#endif
    return new (_HEAP) VMBigInteger(value);
}


pVMBlock Universe::NewBlock( pVMMethod method, pVMFrame context, long arguments) {
    pVMBlock result = new (_HEAP) VMBlock;
    result->SetClass(this->GetBlockClassWithArgs(arguments));

    result->SetMethod(method);
    result->SetContext(context);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMBlock", result->GetObjectSize());
#endif

    return result;
}


pVMClass Universe::NewClass( pVMClass classOfClass) const {
    long numFields = classOfClass->GetNumberOfInstanceFields();
    pVMClass result;
    long additionalBytes = numFields * sizeof(pVMObject);
    if (numFields) result = new (_HEAP, additionalBytes) VMClass(numFields);
    else result = new (_HEAP) VMClass;

    result->SetClass(classOfClass);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMClass", result->GetObjectSize());
#endif

    return result;
}


pVMDouble Universe::NewDouble( double value) const {
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMDouble", sizeof(VMDouble));
#endif
    return new (_HEAP) VMDouble(value);
}


pVMFrame Universe::NewFrame( pVMFrame previousFrame, pVMMethod method) const {
  pVMFrame result = NULL;
#ifdef UNSAFE_FRAME_OPTIMIZATION
  result = method->GetCachedFrame();
  if (result != NULL) {
    method->SetCachedFrame(NULL);
    result->SetPreviousFrame(previousFrame);
    return result;
  }
#endif
    long length = method->GetNumberOfArguments() +
                 method->GetNumberOfLocals()+
                 method->GetMaximumNumberOfStackElements();

    long additionalBytes = length * sizeof(pVMObject);
    result = new (_HEAP, additionalBytes) VMFrame(length);
    result->SetClass(frameClass);
    result->SetMethod(method);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMFrame", result->GetObjectSize());
#endif
    result->SetPreviousFrame(previousFrame);
    result->ResetStackPointer();
    return result;

}


pVMObject Universe::NewInstance( pVMClass  classOfInstance) const {
    //the number of fields for allocation. We have to calculate the clazz
    //field out of this, because it is already taken care of by VMObject
    long numOfFields = classOfInstance->GetNumberOfInstanceFields() - 1;
    //the additional space needed is calculated from the number of fields
    long additionalBytes = numOfFields * sizeof(pVMObject);
    VMObject* result = new (_HEAP, additionalBytes) VMObject(numOfFields);
    result->SetClass(classOfInstance);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION(classOfInstance->GetName()->GetStdString(), result->GetObjectSize());
#endif
    return result;
}

  pVMInteger Universe::NewInteger( long value) const {

#ifdef GENERATE_INTEGER_HISTOGRAM
    integerHist[value/INT_HIST_SIZE] = integerHist[value/INT_HIST_SIZE]+1;
#endif

#ifdef CACHE_INTEGER
   unsigned long index = (unsigned long)value - (unsigned long)INT_CACHE_MIN_VALUE;
    if (index < (unsigned long)(INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE)) {
      return prebuildInts[index];
    }
#endif
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMInteger", sizeof(VMInteger));
#endif

    return new (_HEAP) VMInteger(value);
  }

pVMClass Universe::NewMetaclassClass() const {
    pVMClass result = new (_HEAP) VMClass;
    result->SetClass(new (_HEAP) VMClass);

    pVMClass mclass = result->GetClass();
    mclass->SetClass(result);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMClass", result->GetObjectSize());
#endif

    return result;
}

void Universe::WalkGlobals(VMOBJECT_PTR (*walk)(VMOBJECT_PTR)) {
	nilObject = (VMObject*)walk(nilObject);
	trueObject = (VMObject*)walk(trueObject);
	falseObject = (VMObject*)walk(falseObject);

#ifdef USE_TAGGING
	GlobalBox::updateIntegerBox(static_cast<VMInteger*>(walk(GlobalBox::IntegerBox())));
#endif

	objectClass = static_cast<pVMClass>(walk(objectClass));
	classClass = static_cast<pVMClass>(walk(classClass));
	metaClassClass =  static_cast<pVMClass>(walk(metaClassClass));

	nilClass = static_cast<pVMClass>(walk(nilClass));
	integerClass = static_cast<pVMClass>(walk(integerClass));
	bigIntegerClass = static_cast<pVMClass>(walk(bigIntegerClass));
	arrayClass = static_cast<pVMClass>(walk(arrayClass));
	methodClass = static_cast<pVMClass>(walk(methodClass));
	symbolClass = static_cast<pVMClass>(walk(symbolClass));
	frameClass = static_cast<pVMClass>(walk(frameClass));
	primitiveClass = static_cast<pVMClass>(walk(primitiveClass));
	stringClass = static_cast<pVMClass>(walk(stringClass));
	systemClass = static_cast<pVMClass>(walk(systemClass));
	blockClass = static_cast<pVMClass>(walk(blockClass));
	doubleClass = static_cast<pVMClass>(walk(doubleClass));

#ifdef CACHE_INTEGER
  for (unsigned long i = 0; i < (INT_CACHE_MAX_VALUE - INT_CACHE_MIN_VALUE); i++)
#ifdef USE_TAGGING
    prebuildInts[i] = TAG_INTEGER(INT_CACHE_MIN_VALUE + i);
#else
    prebuildInts[i] = static_cast<pVMInteger>(walk(prebuildInts[i]));
#endif
#endif

	//walk all entries in globals map
	map<pVMSymbol, pVMObject> globs = globals;
	globals.clear();
	map<pVMSymbol, pVMObject>::iterator iter;
	for (iter = globs.begin(); iter != globs.end(); iter++) {
		if (iter->second == NULL)
			continue;

		pVMSymbol key = static_cast<pVMSymbol>(walk(iter->first));
		pVMObject val = walk((VMOBJECT_PTR)iter->second);
		globals[key] = val;
	}
	//walk all entries in symbols map
	map<StdString, pVMSymbol>::iterator symbolIter;
	for (symbolIter = symbolsMap.begin(); symbolIter !=
			symbolsMap.end(); symbolIter++) {
		//insert overwrites old entries inside the internal map
		symbolIter->second = static_cast<pVMSymbol>(walk(symbolIter->second));
	}

	map<long, pVMClass>::iterator bcIter;
	for (bcIter = blockClassesByNoOfArgs.begin(); bcIter !=
			blockClassesByNoOfArgs.end(); bcIter++) {
		bcIter->second = static_cast<pVMClass>(walk(bcIter->second));
	}

  //reassign ifTrue ifFalse Symbols
  symbolIfTrue = symbolsMap["ifTrue:"];
  symbolIfFalse = symbolsMap["ifFalse:"];
}


pVMMethod Universe::NewMethod( pVMSymbol signature, 
                    size_t numberOfBytecodes, size_t numberOfConstants) const {
    //Method needs space for the bytecodes and the pointers to the constants
    long additionalBytes = PADDED_SIZE(numberOfBytecodes + numberOfConstants*sizeof(pVMObject));
#if GC_TYPE==GENERATIONAL
    pVMMethod result = new (_HEAP,additionalBytes, true) 
                VMMethod(numberOfBytecodes, numberOfConstants);
#else
    pVMMethod result = new (_HEAP,additionalBytes) 
                VMMethod(numberOfBytecodes, numberOfConstants);
#endif
    result->SetClass(methodClass);

    result->SetSignature(signature);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMMethod", result->GetObjectSize());
#endif

    return result;
}

pVMString Universe::NewString( const StdString& str) const {
    return NewString(str.c_str());
}

pVMString Universe::NewString( const char* str) const {
    pVMString result = new (_HEAP, PADDED_SIZE(strlen(str) + 1)) VMString(str);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMString", result->GetObjectSize());
#endif
    return result;
}

pVMSymbol Universe::NewSymbol( const StdString& str) {
    return NewSymbol(str.c_str());
}

pVMSymbol Universe::NewSymbol( const char* str ) {
  pVMSymbol result = new (_HEAP, PADDED_SIZE(strlen(str)+1)) VMSymbol(str);
	symbolsMap[str] = result;
#ifdef GENERATE_ALLOCATION_STATISTICS
  LOG_ALLOCATION("VMSymbol", result->GetObjectSize());
#endif
  return result;
}


pVMClass Universe::NewSystemClass() const {
    pVMClass systemClass = new (_HEAP) VMClass();

    systemClass->SetClass(new (_HEAP) VMClass());
    pVMClass mclass = systemClass->GetClass();
    
    mclass->SetClass(metaClassClass);
#ifdef GENERATE_ALLOCATION_STATISTICS
    LOG_ALLOCATION("VMClass", systemClass->GetObjectSize());
#endif

    return systemClass;
}


pVMSymbol Universe::SymbolFor( const StdString& str) {
	map<string,pVMSymbol>::iterator it = symbolsMap.find(str);
  return (it == symbolsMap.end()) ? NewSymbol(str) : it->second;
}


pVMSymbol Universe::SymbolForChars( const char* str) {
	return SymbolFor(str);
}


void Universe::SetGlobal(pVMSymbol name, pVMObject val) {
    globals[name] = val;
}

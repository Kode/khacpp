#include <hxcpp.h>
#include <hx/Scriptable.h>
#include <hx/GC.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>

#include "Cppia.h"

#define DBGLOG(...) { }
//#define DBGLOG printf


namespace hx
{

// todo - TLS
static CppiaCtx *sCurrent = 0;

struct LoopBreak { virtual ~LoopBreak() {} };
LoopBreak sLoopBreak;
struct LoopContinue { virtual ~LoopContinue() {} };
LoopContinue sLoopContinue;
#define JUMP_BREAK 1
//#define FLAG_BREAK 1


#ifdef DEBUG_RETURN_TYPE
int gLastRet = etVoid;
#endif

static bool isNumeric(ExprType t) { return t==etInt || t==etFloat; }

CppiaCtx::CppiaCtx()
{
   stack = new unsigned char[64*1024];
   pointer = &stack[0];
   push((hx::Object *)0);
   frame = pointer;
   sCurrent = this;
   returnJumpBuf = 0;
   loopJumpBuf = 0;
   breakContReturn = 0;
}

CppiaCtx::~CppiaCtx()
{
   delete [] stack;
}


CppiaCtx *CppiaCtx::getCurrent() { return sCurrent; }

void CppiaCtx::mark(hx::MarkContext *__inCtx)
{
   hx::MarkConservative((int *)(stack), (int *)(pointer),__inCtx);
}

void scriptMarkStack(hx::MarkContext *__inCtx)
{
   if (sCurrent)
      sCurrent->mark(__inCtx);
}


CppiaCtx sCppiaCtx;



class ScriptRegistered
{
public:
   std::string  name;
   hx::ScriptableClassFactory factory;
   hx::ScriptFunction  construct;
   ScriptNamedFunction *functions;
   ScriptRegistered *haxeSuper;
   int mDataOffset;

   ScriptRegistered(const std::string &inName, int inDataOffset, ScriptNamedFunction *inFunctions, hx::ScriptableClassFactory inFactory, ScriptFunction inConstruct)
   {
      name = inName;
      mDataOffset = inDataOffset;
      functions = inFunctions;
      factory = inFactory;
      construct = inConstruct;
      haxeSuper = 0;
   }

   void addVtableEntries( std::vector<std::string> &outVtable)
   {
      if (haxeSuper)
         haxeSuper->addVtableEntries(outVtable);

      if (functions)
         for(ScriptNamedFunction *func = functions; func->name; func++)
            outVtable.push_back( func->name );
   }

   ScriptFunction findFunction(const std::string &inName)
   {
      if (functions)
         for(ScriptNamedFunction *f=functions;f->name;f++)
            if (inName == f->name)
               return *f;
      if (haxeSuper)
         return haxeSuper->findFunction(inName);

      return ScriptFunction(0,0);
   }

};

class ScriptRegisteredIntferface
{
public:
   std::string  name;
   const hx::type_info *mType;
   ScriptableInterfaceFactory factory;
   ScriptNamedFunction *functions;

   ScriptRegisteredIntferface(const std::string &inName, ScriptNamedFunction *inFunctions,hx::ScriptableInterfaceFactory inFactory,const hx::type_info *inType)
   {
      functions = inFunctions;
      factory = inFactory;
      name = inName;
      mType = inType;
   }

   ScriptFunction findFunction(const std::string &inName)
   {
      if (functions)
         for(ScriptNamedFunction *f=functions;f->name;f++)
            if (inName == f->name)
               return *f;
      //if (haxeSuper)
      //   return haxeSuper->findFunction(inName);

      return ScriptFunction(0,0);
   }

};


typedef std::map<std::string, ScriptRegistered *> ScriptRegisteredMap;
static ScriptRegisteredMap *sScriptRegistered = 0;
static ScriptRegistered *sObject = 0;

typedef std::map<std::string, ScriptRegisteredIntferface *> ScriptRegisteredInterfaceMap;
static ScriptRegisteredInterfaceMap *sScriptRegisteredInterface = 0;


// TODO - toString?
/*
class Object_obj__scriptable : public Object
{
   typedef Object_obj__scriptable __ME;
   typedef Object super;
   typedef Object __superString;

   void __construct() { }
   HX_DEFINE_SCRIPTABLE(HX_ARR_LIST0)
   HX_DEFINE_SCRIPTABLE_DYNAMIC;
};
*/


void ScriptableRegisterClass( String inName, int inDataOffset, ScriptNamedFunction *inFunctions, hx::ScriptableClassFactory inFactory, hx::ScriptFunction inConstruct)
{
   DBGLOG("ScriptableRegisterClass %s\n", inName.__s);
   if (!sScriptRegistered)
      sScriptRegistered = new ScriptRegisteredMap();
   ScriptRegistered *registered = new ScriptRegistered(inName.__s,inDataOffset, inFunctions,inFactory, inConstruct);
   (*sScriptRegistered)[inName.__s] = registered;
   //printf("Registering %s -> %p\n",inName.__s,(*sScriptRegistered)[inName.__s]);
}


void ScriptableRegisterInterface( String inName, ScriptNamedFunction *inFunctions, const hx::type_info *inType,
                                 hx::ScriptableInterfaceFactory inFactory )
{
   DBGLOG("ScriptableInterfaceFactory %s\n",inName.__s);
   if (!sScriptRegisteredInterface)
      sScriptRegisteredInterface = new ScriptRegisteredInterfaceMap();
   ScriptRegisteredIntferface *registered = new ScriptRegisteredIntferface(inName.__s, inFunctions, inFactory,inType);
   (*sScriptRegisteredInterface)[inName.__s] = registered;
}


static int sTypeSize[] = { 0, 0, sizeof(hx::Object *), sizeof(String), sizeof(Float), sizeof(int) };

hx::Object *createClosure(CppiaCtx *ctx, struct ScriptCallable *inFunction);
hx::Object *createMemberClosure(hx::Object *, struct ScriptCallable *inFunction);
hx::Object *createEnumClosure(struct CppiaEnumConstructor &inContructor);
void cppiaClassMark(CppiaClassInfo *inClass,hx::MarkContext *__inCtx);
void cppiaClassVisit(CppiaClassInfo *inClass,hx::VisitContext *__inCtx);

struct TypeData
{
   String           name;
   Class            haxeClass;
   CppiaClassInfo       *cppiaClass;
   ExprType         expressionType;
   ScriptRegistered *haxeBase;
   ScriptRegisteredIntferface *interfaceBase;
   bool             linked;
   bool             isInterface;
   ArrayType        arrayType;

   TypeData(String inData)
   {
      Array<String> parts = inData.split(HX_CSTRING("::"));
      if (parts[0].length==0)
         parts->shift();
      name = parts->join(HX_CSTRING("."));
      cppiaClass = 0;
      haxeClass = null();
      haxeBase = 0;
      linked = false;
      arrayType = arrNotArray;
      interfaceBase = 0;
      isInterface = false;
   }
   bool isClassOf(Dynamic inInstance)
   {
      // TODO script class
      return __instanceof(inInstance,haxeClass);
   }
   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(name);
      HX_MARK_MEMBER(haxeClass);
      if (cppiaClass)
         cppiaClassMark(cppiaClass,__inCtx);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(name);
      HX_VISIT_MEMBER(haxeClass);
      if (cppiaClass)
         cppiaClassVisit(cppiaClass,__inCtx);
   }

   void link(CppiaData &inData);
};

struct StackLayout;

void cppiaClassInit(CppiaClassInfo *inClass, CppiaCtx *ctx, int inPhase);



struct CppiaData
{
   Array< String > strings;
   std::vector< std::string > cStrings;
   std::vector< TypeData * > types;
   std::vector< CppiaClassInfo * > classes;
   std::vector< CppiaExpr * > markable;
   CppiaExpr   *main;

   StackLayout *layout;
   CppiaClassInfo  *linkingClass;
   const char *creatingClass;
   const char *creatingFunction;

   CppiaData()
   {
      main = 0;
      layout = 0;
      creatingClass = 0;
      creatingFunction = 0;
      strings = Array_obj<String>::__new(0,0);
   }

   //CppiaClassInfo *findClass(String inName);

   ~CppiaData();

   void link();

   void setDebug(CppiaExpr *outExpr, int inFileId, int inLine)
   {
      outExpr->className = creatingClass;
      outExpr->functionName = creatingFunction;
      outExpr->filename = cStrings[inFileId].c_str();
      outExpr->line = inLine;
   }

   void boot(CppiaCtx *ctx)
   {
      // boot (statics)
      for(int i=0;i<classes.size();i++)
         cppiaClassInit(classes[i],ctx,0);
      // run __init__
      for(int i=0;i<classes.size();i++)
         cppiaClassInit(classes[i],ctx,1);
   }


   void where(CppiaExpr *e);
   void mark(hx::MarkContext *ctx);
   void visit(hx::VisitContext *ctx);

   const char *identStr(int inId) { return strings[inId].__s; }
   const char *typeStr(int inId) { return types[inId]->name.c_str(); }
};


enum VarLocation
{
   locObj,
   locThis,
   locStack,
   locAbsolute,
};


std::map<int,int> sVarIdNameMap;
struct CppiaStackVar
{
   int  nameId;
   int  id;
   bool capture;
   int  typeId;
   int  stackPos;
   int  fromStackPos;
   int  capturePos;


   ExprType expressionType;

   CppiaStackVar()
   {
      nameId = 0;
      id = 0;
      capture = false;
      typeId = 0;
      stackPos = 0;
      fromStackPos = 0;
      capturePos = 0;
      expressionType = etNull;
   }

   CppiaStackVar(CppiaStackVar *inVar,int &ioSize, int &ioCaptureSize)
   {
      nameId = inVar->nameId;
      id = inVar->id;
      sVarIdNameMap[id] = nameId;
      capture = inVar->capture;
      typeId = inVar->typeId;
      expressionType = inVar->expressionType;

      fromStackPos = inVar->stackPos;
      stackPos = ioSize;
      capturePos = ioCaptureSize;
      ioSize += sTypeSize[expressionType];
      ioCaptureSize += sTypeSize[expressionType];
   }


   void fromStream(CppiaStream &stream)
   {
      nameId = stream.getInt();
      id = stream.getInt();
      capture = stream.getBool();
      typeId = stream.getInt();
   }

   void set(CppiaCtx *inCtx,Dynamic inValue)
   {
      switch(expressionType)
      {
         case etInt:
            *(int *)(inCtx->frame + stackPos) = inValue;
            break;
         case etFloat:
            *(Float *)(inCtx->frame + stackPos) = inValue;
            break;
         case etString:
            *(String *)(inCtx->frame + stackPos) = inValue;
            break;
         case etObject:
            *(hx::Object **)(inCtx->frame + stackPos) = inValue.mPtr;
            break;
         case etVoid:
         case etNull:
            break;
      }
   }

   void markClosure(char *inBase, hx::MarkContext *__inCtx)
   {
      switch(expressionType)
      {
         case etString:
            HX_MARK_MEMBER(*(String *)(inBase + capturePos));
            break;
         case etObject:
            HX_MARK_MEMBER(*(hx::Object **)(inBase + capturePos));
            break;
         default: ;
      }
   }

   void visitClosure(char *inBase, hx::VisitContext *__inCtx)
   {
      switch(expressionType)
      {
         case etString:
            HX_VISIT_MEMBER(*(String *)(inBase + capturePos));
            break;
         case etObject:
            HX_VISIT_MEMBER(*(Dynamic *)(inBase + capturePos));
            break;
         default: ;
      }
   }


   void link(CppiaData &inData);
};




struct StackLayout
{
   std::map<int,CppiaStackVar *> varMap;
   std::vector<CppiaStackVar *>  captureVars;
   StackLayout *parent;
   ExprType    returnType;
   int captureSize;
   int size;

   // 'this' pointer is in slot 0 and captureSize(0) ...
   StackLayout(StackLayout *inParent) :
      size( sizeof(void *) ), captureSize(sizeof(void *)), parent(inParent)
   {
   }


   void dump(Array<String> &inStrings, std::string inIndent)
   {
      printf("%sCapture:\n",inIndent.c_str());
      for(int i=0;i<captureVars.size();i++)
         printf("%s %s\n", inIndent.c_str(), inStrings[captureVars[i]->nameId].__s );
      if (parent)
         parent->dump(inStrings,inIndent + "   ");
   }


   CppiaStackVar *findVar(int inId)
   {
      if (varMap[inId])
         return varMap[inId];
      CppiaStackVar *var = parent ? parent->findVar(inId) : 0;
      if (!var)
         return 0;

      CppiaStackVar *closureVar = new CppiaStackVar(var,size,captureSize);
      varMap[inId] = closureVar;
      captureVars.push_back(closureVar);
      return closureVar;
   }

};

void CppiaStackVar::link(CppiaData &inData)
{
   expressionType = inData.types[typeId]->expressionType;
   inData.layout->varMap[id] = this;
   stackPos = inData.layout->size;
   inData.layout->size += sTypeSize[expressionType];
}



class CppiaObject : public hx::Object
{
public:
   CppiaData *data;
   CppiaObject(CppiaData *inData)
   {
      data = inData;
      GCSetFinalizer( this, onRelease );
   }
   static void onRelease(hx::Object *inObj)
   {
      delete ((CppiaObject *)inObj)->data;
   }
   void __Mark(hx::MarkContext *ctx) { data->mark(ctx); }
   void __Visit(hx::VisitContext *ctx) { data->visit(ctx); }
};

struct ArgInfo
{
   int  nameId;
   bool optional;
   int  typeId;
};


// --- CppiaCtx functions ----------------------------------------

#ifdef DEBUG_RETURN_TYPE
   #define DEBUG_RETURN_TYPE_CHECK \
       if (gLastRet!=CHECK && CHECK!=etVoid) { printf("BAD RETURN TYPE, got %d, expected %d!\n",gLastRet,CHECK); CPPIA_CHECK(0); }
#else
   #define DEBUG_RETURN_TYPE_CHECK
#endif


#define GET_RETURN_VAL(RET,CHECK) \
   CPPIA_STACK_FRAME(((CppiaExpr *)vtable)); \
   jmp_buf here; \
   jmp_buf *old = returnJumpBuf; \
   returnJumpBuf = &here; \
   if (setjmp(here)) \
   { \
       DEBUG_RETURN_TYPE_CHECK \
       returnJumpBuf = old; \
       RET; \
   } \
   ((CppiaExpr *)vtable)->runVoid(this); \
   returnJumpBuf = old;

 

void CppiaCtx::runVoid(void *vtable)
{
   GET_RETURN_VAL(return,etVoid);
   /* Reached end of routine without return */
}

int CppiaCtx::runInt(void *vtable)
{
   GET_RETURN_VAL(return getInt(), etInt );
   //printf("No Int return?\n");
   // Should not really get here...
   return 0;
}
Float CppiaCtx::runFloat(void *vtable)
{
   GET_RETURN_VAL(return getFloat(),etFloat );
   // Should not really get here...
   //printf("No Float return?\n");
   return 0;
}
String CppiaCtx::runString(void *vtable)
{
   GET_RETURN_VAL(return getString(),etString );
   // Should not really get here...
   //printf("No String return?\n");
   return null();
}
Dynamic CppiaCtx::runObject(void *vtable)
{
   GET_RETURN_VAL(return getObject(),etObject );
   //printf("No Object return?\n");
   return null();
}
hx::Object *CppiaCtx::runObjectPtr(void *vtable)
{
   GET_RETURN_VAL(return getObjectPtr(),etObject );
   //printf("No Object return?\n");
   return 0;
}


int runContextConvertInt(CppiaCtx *ctx, ExprType inType, void *inFunc)
{
   switch(inType)
   {
      case etInt: return ctx->runInt(inFunc);
      case etFloat: return ctx->runFloat(inFunc);
      case etObject: return ctx->runObject(inFunc);
      default:
          ctx->runVoid(inFunc);
   }
   return 0;
}


Float runContextConvertFloat(CppiaCtx *ctx, ExprType inType, void *inFunc)
{
   switch(inType)
   {
      case etInt: return ctx->runInt(inFunc);
      case etFloat: return ctx->runFloat(inFunc);
      case etObject: return ctx->runObject(inFunc);
      default:
          ctx->runVoid(inFunc);
   }
   return 0;
}

String runContextConvertString(CppiaCtx *ctx, ExprType inType, void *inFunc)
{
   switch(inType)
   {
      case etInt: return String(ctx->runInt(inFunc));
      case etFloat: return String(ctx->runFloat(inFunc));
      case etObject: return ctx->runObject(inFunc);
      case etString: return ctx->runString(inFunc);
      default:
          ctx->runVoid(inFunc);
   }
   return 0;
}

hx::Object *runContextConvertObject(CppiaCtx *ctx, ExprType inType, void *inFunc)
{
   switch(inType)
   {
      case etInt: return Dynamic(ctx->runInt(inFunc)).mPtr;
      case etFloat: return Dynamic(ctx->runFloat(inFunc)).mPtr;
      case etObject: return ctx->runObject(inFunc).mPtr;
      case etString: return Dynamic(ctx->runString(inFunc)).mPtr;
      default:
          ctx->runVoid(inFunc);
   }
   return 0;
}




// --- CppiaDynamicExpr ----------------------------------------
// Delegates to 'runObject'

struct CppiaDynamicExpr : public CppiaExpr
{
   CppiaDynamicExpr(const CppiaExpr *inSrc=0) : CppiaExpr(inSrc) {}

   const char *getName() { return "CppiaDynamicExpr"; }

   virtual int         runInt(CppiaCtx *ctx)    {
      hx::Object *obj = runObject(ctx);
      return obj->__ToInt();
   }
   virtual Float       runFloat(CppiaCtx *ctx) { return runObject(ctx)->__ToDouble(); }
   virtual ::String    runString(CppiaCtx *ctx)
   {
      hx::Object *result = runObject(ctx);
      return result ? result->toString() : String();
   }
   virtual void        runVoid(CppiaCtx *ctx)   { runObject(ctx); }
   virtual hx::Object *runObject(CppiaCtx *ctx) = 0;
};

// --- CppiaVoidExpr ----------------------------------------
// Delegates to 'runInt'
struct CppiaVoidExpr : public CppiaExpr
{
   CppiaVoidExpr(const CppiaExpr *inSrc=0) : CppiaExpr(inSrc) {}

   const char *getName() { return "CppiaVoidExpr"; }

   ExprType getType() { return etVoid; }

   virtual int  runInt(CppiaCtx *ctx) { runVoid(ctx); return 0; }
   virtual Float       runFloat(CppiaCtx *ctx) { runVoid(ctx); return 0.0; }
   virtual ::String    runString(CppiaCtx *ctx) { runVoid(ctx); return String(); }
   virtual hx::Object *runObject(CppiaCtx *ctx) { runVoid(ctx); return 0; }
   virtual void        runVoid(CppiaCtx *ctx) = 0;
};




// --- CppiaIntExpr ----------------------------------------
// Delegates to 'runInt'

struct CppiaIntExpr : public CppiaExpr
{
   CppiaIntExpr(const CppiaExpr *inSrc=0) : CppiaExpr(inSrc) {}

   const char *getName() { return "CppiaIntExpr"; }
   ExprType getType() { return etInt; }

   void runVoid(CppiaCtx *ctx) { runInt(ctx); }
   Float runFloat(CppiaCtx *ctx) { return runInt(ctx); }
   hx::Object *runObject(CppiaCtx *ctx) { return Dynamic(runInt(ctx)).mPtr; }
   String runString(CppiaCtx *ctx) { return String(runInt(ctx)); }

   int runInt(CppiaCtx *ctx) = 0;
};


// --- CppiaBoolExpr ----------------------------------------
// Delegates to 'runInt'

struct CppiaBoolExpr : public CppiaIntExpr
{
   CppiaBoolExpr(const CppiaExpr *inSrc=0) : CppiaIntExpr(inSrc) {}

   const char *getName() { return "CppiaBoolExpr"; }
   hx::Object *runObject(CppiaCtx *ctx) { return Dynamic(runInt(ctx) ? true : false).mPtr; }
   String runString(CppiaCtx *ctx) { return runInt(ctx)?HX_CSTRING("true") : HX_CSTRING("false");}
};


// ------------------------------------------------------



CppiaExpr *createCppiaExpr(CppiaStream &inStream);
CppiaExpr *createStaticAccess(CppiaExpr *inSrc, ExprType inType, void *inPtr);

static void ReadExpressions(Expressions &outExpressions, CppiaStream &stream,int inN=-1)
{
   int count = inN>=0 ? inN : stream.getInt();
   outExpressions.resize(count);

   for(int i=0;i<count;i++)
      outExpressions[i] =  createCppiaExpr(stream);
}



static void LinkExpressions(Expressions &ioExpressions, CppiaData &data)
{
   for(int i=0;i<ioExpressions.size();i++)
      ioExpressions[i] = ioExpressions[i]->link(data);
}


CppiaExpr *convertToFunction(CppiaExpr *inExpr);

struct CppiaFunction
{
   CppiaData &cppia;
   int       nameId;
   bool      isStatic;
   bool      isDynamic;
   int       returnType;
   int       argCount;
   int       vtableSlot;
   bool      linked;
   std::string name;
   std::vector<ArgInfo> args;
   CppiaExpr *funExpr;

   CppiaFunction(CppiaData *inCppia,bool inIsStatic,bool inIsDynamic) :
      cppia(*inCppia), isStatic(inIsStatic), isDynamic(inIsDynamic), funExpr(0)
   {
      linked = false;
      vtableSlot = -1;
   }

   void setVTableSlot(int inSlot) { vtableSlot = inSlot; }

   String getName() { return cppia.strings[nameId]; }

   void load(CppiaStream &stream,bool inExpectBody)
   {
      nameId = stream.getInt();
      name = cppia.strings[ nameId ].__s;
      stream.cppiaData->creatingFunction = name.c_str();
      returnType = stream.getInt();
      argCount = stream.getInt();
      DBGLOG("  Function %s(%d) : %s %s%s\n", name.c_str(), argCount, cppia.typeStr(returnType), isStatic?"static":"instance", isDynamic ? " DYNAMIC": "");
      args.resize(argCount);
      for(int a=0;a<argCount;a++)
      {
         ArgInfo arg = args[a];
         arg.nameId = stream.getInt();
         arg.optional = stream.getBool();
         arg.typeId = stream.getInt();
         DBGLOG("    arg %c%s:%s\n", arg.optional?'?':' ', cppia.identStr(arg.nameId), cppia.typeStr(arg.typeId) );
      }
      if (inExpectBody)
         funExpr = createCppiaExpr(stream);
      stream.cppiaData->creatingFunction = 0;
   }
   void link( )
   {
      if (!linked)
      {
         linked = true;
         if (funExpr)
            funExpr = funExpr->link(cppia);
      }
   }
};


struct CppiaVar
{
   enum Access { accNormal, accNo, accResolve, accCall, accRequire } ;
   TypeData  *type;
   bool      isStatic;
   bool      isVirtual;
   Access    readAccess;
   Access    writeAccess;
   int       nameId;
   int       typeId;
   int       offset;
   String    name;
   FieldStorage storeType;
   CppiaFunction *dynamicFunction;
   ExprType   exprType;

   CppiaExpr *init;
   Dynamic   objVal;
   int       intVal;
   Float     floatVal;
   String    stringVal;
   void      *valPointer;
   

   CppiaVar(bool inIsStatic) : isStatic(inIsStatic)
   {
      clear();
   }

   CppiaVar(CppiaFunction *inDynamicFunction)
   {
      clear();
      isStatic = inDynamicFunction->isStatic;
      dynamicFunction = inDynamicFunction;
      nameId = dynamicFunction->nameId;
      exprType = etObject;
      storeType = fsObject;
   }

   void clear()
   {
      type = 0;
      nameId = 0;
      typeId = 0;
      offset = 0;
      type = 0;
      objVal.mPtr = 0;
      intVal = 0;
      floatVal = 0;
      stringVal = 0;
      valPointer = 0;
      storeType = fsUnknown;
      dynamicFunction = 0;
      init = 0;
   }

   void load(CppiaStream &stream)
   {
      readAccess = getAccess(stream);
      writeAccess = getAccess(stream);
      isVirtual = stream.getBool();
      nameId = stream.getInt();
      typeId = stream.getInt();
      if (stream.getInt())
         init = createCppiaExpr(stream);
   }

   ExprType getType() { return exprType; }

   // Static...
   void linkVarTypes(CppiaData &cppia)
   {
      if (dynamicFunction)
      {
         nameId = dynamicFunction->nameId;
      }
      type = cppia.types[typeId];
      name = cppia.strings[nameId];
      exprType = typeId==0 ? etObject : type->expressionType;

      if (!isVirtual)
      {
         switch(exprType)
         {
            case etInt: valPointer = &intVal; storeType=fsInt; break;
            case etFloat: valPointer = &floatVal; storeType=fsFloat; break;
            case etString: valPointer = &stringVal;storeType=fsString;  break;
            case etObject: valPointer = &objVal;storeType=fsObject;  break;
            default:
              ;
         }
      }
   }

   Dynamic getStaticValue()
   {
      switch(storeType)
      {
         case fsByte: return *(unsigned char *)(valPointer);
         case fsInt: return *(int *)(valPointer);
         case fsBool: return *(bool *)(valPointer);
         case fsFloat: return *(Float *)(valPointer);
         case fsString: return *(String *)(valPointer);
         case fsObject: return *(hx::Object **)(valPointer);
         case fsUnknown:
            break;
      }
      return null();
   }


   Dynamic setStaticValue(Dynamic inValue)
   {
      switch(storeType)
      {
         case fsByte: *(unsigned char *)(valPointer) = inValue; return inValue;
         case fsInt: *(int *)(valPointer) = inValue; return inValue;
         case fsBool: *(bool *)(valPointer) = inValue; return inValue;
         case fsFloat: *(Float *)(valPointer) = inValue; return inValue;
         case fsString: *(String *)(valPointer) = inValue; return inValue;
         case fsObject: *(hx::Object **)(valPointer) = inValue.mPtr; return inValue;
         case fsUnknown:
            break;
      }
      return null();
   }




   CppiaExpr *createAccess(CppiaExpr *inSrc)
   {
      if (valPointer)
         return createStaticAccess(inSrc, exprType, valPointer);
      if (isVirtual)
         throw "Direct access to extern field";
      throw "Unlinked static variable";
      return 0;
   }

   void linkVarTypes(CppiaData &cppia, int &ioOffset)
   {
      DBGLOG("linkVarTypes %p\n",dynamicFunction);
      if (dynamicFunction)
      {
         //dynamicFunction->linkTypes(cppia);
         nameId = dynamicFunction->nameId;
      }
      type = cppia.types[typeId];
      if (!isVirtual)
      {
         offset = ioOffset;
         exprType = typeId==0 ? etObject : type->expressionType;
         
         switch(exprType)
         {
            case etInt: ioOffset += sizeof(int); storeType=fsInt; break;
            case etFloat: ioOffset += sizeof(Float);storeType=fsFloat;  break;
            case etString: ioOffset += sizeof(String);storeType=fsString;  break;
            case etObject: ioOffset += sizeof(hx::Object *);storeType=fsObject;  break;
            case etVoid:
            case etNull:
               break;
         }
      }
   }

   void createDynamic(hx::Object *inBase)
   {
      *(hx::Object **)((char *)inBase+offset) = createMemberClosure(inBase,(ScriptCallable*)dynamicFunction->funExpr);
   }


   Dynamic getValue(hx::Object *inThis)
   {
      char *base = ((char *)inThis) + offset;
 
      switch(storeType)
      {
         case fsByte: return *(unsigned char *)(base);
         case fsInt: return *(int *)(base);
         case fsBool: return *(bool *)(base);
         case fsFloat: return *(Float *)(base);
         case fsString: return *(String *)(base);
         case fsObject: return *(hx::Object **)(base);
         case fsUnknown:
            break;
      }
      return null();
   }

   Dynamic setValue(hx::Object *inThis, Dynamic inValue)
   {
      char *base = ((char *)inThis) + offset;
 
      switch(storeType)
      {
         case fsByte: *(unsigned char *)(base) = inValue; return inValue;
         case fsInt: *(int *)(base) = inValue; return inValue;
         case fsBool: *(bool *)(base) = inValue; return inValue;
         case fsFloat: *(Float *)(base) = inValue; return inValue;
         case fsString: *(String *)(base) = inValue; return inValue;
         case fsObject: *(hx::Object **)(base) = inValue.mPtr; return inValue;
         case fsUnknown:
            break;
      }
      return null();
   }


   void link(CppiaData &inData)
   {
      if (dynamicFunction)
         dynamicFunction->link();
      type = inData.types[typeId];
      exprType = typeId==0 ? etObject : type->expressionType;
      name = inData.strings[nameId];

      if (init)
         init = init->link(inData);
   }

   static Access getAccess(CppiaStream &stream)
   {
      std::string tok = stream.getToken();
      if (tok.size()!=1)
         throw "bad var access length";
      switch(tok[0])
      {
         case 'N': return accNormal;
         case '!': return accNo;
         case 'R': return accResolve;
         case 'C': return accCall;
         case '?': return accRequire;
      }
      throw "bad access code";
      return accNormal;
   }

   void runInit(CppiaCtx *ctx)
   {
      if (isStatic)
      {
         if (dynamicFunction)
            objVal = createMemberClosure(0,(ScriptCallable*)dynamicFunction->funExpr);
         else if (init)
            switch(exprType)
            {
               case etInt: intVal = init->runInt(ctx); break;
               case etFloat: floatVal = init->runFloat(ctx); break;
               case etString: stringVal = init->runString(ctx); break;
               case etObject: objVal = init->runObject(ctx); break;
               default: ;
            }
      }
   }

   inline void mark(hx::Object *inThis,hx::MarkContext *__inCtx)
   {
      switch(storeType)
      {
         case fsString: HX_MARK_MEMBER(*(String *)((char *)inThis + offset)); break;
         case fsObject: HX_MARK_MEMBER(*(Dynamic*)((char *)inThis + offset)); break;
         default:;
      }
   }
   inline void visit(hx::Object *inThis,hx::VisitContext *__inCtx)
   {
      switch(storeType)
      {
         case fsString: HX_VISIT_MEMBER(*(String *)((char *)inThis + offset)); break;
         case fsObject: HX_VISIT_MEMBER(*(Dynamic *)((char *)inThis + offset)); break;
         default:;
      }
   }

   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(stringVal);
      HX_MARK_MEMBER(objVal);
      HX_MARK_MEMBER(name);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(stringVal);
      HX_VISIT_MEMBER(objVal);
      HX_VISIT_MEMBER(name);
   }
};


struct CppiaConst
{
   enum Type { cInt, cFloat, cString, cBool, cNull, cThis, cSuper };

   Type type;
   int  ival;
   Float  dval;

   CppiaConst() : type(cNull), ival(0), dval(0) { }

   void fromStream(CppiaStream &stream)
   {
      std::string tok = stream.getToken();
      if (tok[0]=='i')
      {
         type = cInt;
         dval = ival = atoi(&tok[1]);
      }
      else if (tok=="true")
      {
         type = cInt;
         dval = ival = 1;
      }
      else if (tok=="false")
      {
         type = cInt;
         dval = ival = 0;
      }
      else if (tok[0]=='f')
      {
         type = cFloat;
         dval = atof(&tok[1]);
      }
      else if (tok[0]=='s')
      {
         type = cString;
         ival = atoi(&tok[1]);
      }
      else if (tok=="NULL")
         type = cNull;
      else if (tok=="THIS")
         type = cThis;
      else if (tok=="SUPER")
         type = cSuper;
      else
         throw "unknown const value";
   }
};


class CppiaEnumBase : public EnumBase_obj
{
public:
   CppiaClassInfo *classInfo; 

   CppiaEnumBase(CppiaClassInfo *inInfo) : classInfo(inInfo) { }

   ::hx::ObjectPtr<Class_obj > __GetClass() const;
	::String GetEnumName( ) const;
	::String __ToString() const;
};



struct CppiaEnumConstructor
{
   struct Arg
   {
      Arg(int inNameId, int inTypeId) : nameId(inNameId), typeId(inTypeId) { }
      int nameId;
      int typeId;
   };
 
   std::vector<Arg> args;
   CppiaClassInfo   *classInfo;
   int              nameId;
   Dynamic          value;
   int              index;
   String           name;

   CppiaEnumConstructor(CppiaData &inData, CppiaStream &inStream, CppiaClassInfo *inClassInfo)
   {
      classInfo = inClassInfo;
      nameId = inStream.getInt();
      index = -1;
      name = String();
      int argCount = inStream.getInt();
      for(int a=0;a<argCount;a++)
      {
         int nameId = inStream.getInt();
         int typeId = inStream.getInt();
         args.push_back( Arg(nameId,typeId) );
      }
         
   }
   hx::Object *create( Array<Dynamic> inArgs )
   {
      bool ok = inArgs.mPtr ? (inArgs->length==args.size()) : args.size()==0;
      if (!ok)
         throw Dynamic(HX_CSTRING("Bad enum arg count"));
      if (args.size()==0)
         return value.mPtr;
      EnumBase_obj *result = new CppiaEnumBase(classInfo);
      result->Set(name, index, inArgs);
      return result;
   }
};


void runFunExpr(CppiaCtx *ctx, CppiaExpr *inFunExpr, hx::Object *inThis, Expressions &inArgs );
hx::Object *runFunExprDynamic(CppiaCtx *ctx, CppiaExpr *inFunExpr, hx::Object *inThis, Array<Dynamic> &inArgs );
void runFunExprDynamicVoid(CppiaCtx *ctx, CppiaExpr *inFunExpr, hx::Object *inThis, Array<Dynamic> &inArgs );

Class_obj *createCppiaClass(CppiaClassInfo *);
void  linkCppiaClass(Class_obj *inClass, CppiaData &cppia, String inName);


typedef std::vector<CppiaFunction *> Functions;
typedef std::map<std::string, CppiaExpr *> FunctionMap;

struct CppiaClassInfo
{
   CppiaData &cppia;
   std::vector<int> implements;
   bool      isInterface;
   bool      isLinked;
   bool      isEnum;
   int       typeId;
   TypeData  *type;
   int       superId;
   TypeData *superType;
   int       classSize;
   int       extraData;
   int       dynamicMapOffset;
   void      **vtable;
   std::string name;
   std::map<std::string, void **> interfaceVTables;
   Class     mClass;

   ScriptRegistered *haxeBase;

   Functions memberFunctions;
   FunctionMap memberGetters;
   FunctionMap memberSetters;
   std::vector<CppiaVar *> memberVars;
   std::vector<CppiaVar *> dynamicFunctions;

   Functions staticFunctions;
   FunctionMap staticGetters;
   FunctionMap staticSetters;
   std::vector<CppiaVar *> staticVars;
   std::vector<CppiaVar *> staticDynamicFunctions;

   std::vector<CppiaEnumConstructor *> enumConstructors;

   CppiaFunction *newFunc;
   CppiaExpr     *initFunc;
   CppiaExpr     *enumMeta;

   CppiaClassInfo(CppiaData &inCppia) : cppia(inCppia)
   {
      isLinked = false;
      haxeBase = 0;
      extraData = 0;
      classSize = 0;
      newFunc = 0;
      initFunc = 0;
      enumMeta = 0;
      isInterface = false;
      superType = 0;
      typeId = 0;
      vtable = 0;
      type = 0;
      mClass.mPtr = 0;
      dynamicMapOffset = 0;
   }

   /*
   class CppiaClass *getCppiaClass()
   {
      return (class CppiaClass *)mClass.mPtr;
   }
   */

   hx::Object *createInstance(CppiaCtx *ctx,Expressions &inArgs, bool inCallNew = true)
   {
      hx::Object *obj = haxeBase->factory(vtable,extraData);

      createDynamicFunctions(obj);

      if (newFunc && inCallNew)
         runFunExpr(ctx, newFunc->funExpr, obj, inArgs );

      return obj;
   }

   hx::Object *createInstance(CppiaCtx *ctx,Array<Dynamic> &inArgs)
   {
      hx::Object *obj = haxeBase->factory(vtable,extraData);

      createDynamicFunctions(obj);

      if (newFunc)
         runFunExprDynamicVoid(ctx, newFunc->funExpr, obj, inArgs );

      return obj;
   }


   inline void createDynamicFunctions(hx::Object *inThis)
   {
      if (dynamicMapOffset)
         *(hx::FieldMap **)( (char *)inThis + dynamicMapOffset ) =  hx::FieldMapCreate();

      for(int d=0;d<dynamicFunctions.size();d++)
         dynamicFunctions[d]->createDynamic(inThis);
   }


   int __GetType() { return isEnum ? vtEnum : vtClass; }

   int getEnumIndex(String inName)
   {
      for(int i=0;i<enumConstructors.size();i++)
      {
        if (enumConstructors[i]->name==inName)
           return i;
      }

      throw Dynamic(HX_CSTRING("Bad enum index"));
      return 0;
   }

   bool implementsInterface(CppiaClassInfo *inInterface)
   {
      for(int i=0;i<implements.size();i++)
         if (implements[i] == inInterface->typeId)
            return true;
      if (!superId)
         return false;

      TypeData *superType = cppia.types[ superId ];
      if (superType->cppiaClass)
         return superType->cppiaClass->implementsInterface(inInterface);
      return false;
   }


   CppiaExpr *findFunction(bool inStatic,int inId)
   {
      Functions &funcs = inStatic ? staticFunctions : memberFunctions;
      for(int i=0;i<funcs.size();i++)
      {
         if (funcs[i]->nameId == inId)
            return funcs[i]->funExpr;
      }
      return 0;
   }

   CppiaExpr *findInterfaceFunction(const std::string &inName)
   {
      Functions &funcs = memberFunctions;
      for(int i=0;i<funcs.size();i++)
      {
         if ( cppia.strings[funcs[i]->nameId].__s == inName)
            return funcs[i]->funExpr;
      }
      return 0;
   }

   CppiaExpr *findFunction(bool inStatic, const String &inName)
   {
      Functions &funcs = inStatic ? staticFunctions : memberFunctions;
      for(int i=0;i<funcs.size();i++)
      {
         if ( cppia.strings[funcs[i]->nameId] == inName)
            return funcs[i]->funExpr;
      }
      return 0;
   }

   inline CppiaExpr *findFunction(FunctionMap &inMap,const String &inName)
   {
      FunctionMap::iterator it = inMap.find(inName.__s);
      if (it!=inMap.end())
         return it->second;
      return 0;

   }

   inline CppiaExpr *findMemberGetter(const String &inName)
      { return findFunction(memberGetters,inName); }

   inline CppiaExpr *findMemberSetter(const String &inName)
      { return findFunction(memberSetters,inName); }

   inline CppiaExpr *findStaticGetter(const String &inName)
      { return findFunction(staticGetters,inName); }

   inline CppiaExpr *findStaticSetter(const String &inName)
      { return findFunction(staticSetters,inName); }


   bool getField(hx::Object *inThis, String inName, bool inCallProp, Dynamic &outValue)
   {
      if (inCallProp)
      {
         CppiaExpr *getter = findMemberGetter(inName);
         if (getter)
         {
            Array<Dynamic> empty;
            outValue.mPtr = runFunExprDynamic(CppiaCtx::getCurrent(),getter,inThis, empty);
            return true;
         }
      }

      CppiaExpr *closure = findFunction(false,inName);
      if (closure)
      {
         outValue.mPtr = createMemberClosure(inThis,(ScriptCallable*)closure);
         return true;
      }

      // Look for dynamic function (variable)
      for(int i=0;i<dynamicFunctions.size();i++)
      {
         if (cppia.strings[ dynamicFunctions[i]->nameId  ]==inName)
         {
            CppiaVar *d = dynamicFunctions[i];

            outValue = d->getValue(inThis);

            return true;
         }
      }

      for(int i=0;i<memberVars.size();i++)
      {
         CppiaVar &var = *memberVars[i];
         if (var.name==inName)
         {
            outValue = var.getValue(inThis);
            return true;
         }
      }

      hx::FieldMap *map = dynamicMapOffset ? *(hx::FieldMap **)( (char *)inThis + dynamicMapOffset ) :
                           inThis->__GetFieldMap();
      if (map)
      {
         if (hx::FieldMapGet(map,inName,outValue))
            return true;
      }

      //printf("Get field not found (%s) %s\n", inThis->toString().__s,inName.__s);
      return false;
   }

   bool setField(hx::Object *inThis, String inName, Dynamic inValue, bool inCallProp, Dynamic &outValue)
   {
      if (inCallProp)
      {
         //printf("Set field %s %s = %s\n", inThis->toString().__s, inName.__s, inValue->toString().__s);
         CppiaExpr *setter = findMemberSetter(inName);
         if (setter)
         {
            Array<Dynamic> args = Array_obj<Dynamic>::__new(1,1);
            args[0] = inValue;
            outValue.mPtr = runFunExprDynamic(CppiaCtx::getCurrent(),setter,inThis, args);
            return true;
         }
      }


      // Look for dynamic function (variable)
      for(int i=0;i<dynamicFunctions.size();i++)
      {
         if (cppia.strings[ dynamicFunctions[i]->nameId  ]==inName)
         {
            CppiaVar *d = dynamicFunctions[i];
            outValue = d->setValue(inThis,inValue);
            return true;
         }
      }

      for(int i=0;i<memberVars.size();i++)
      {
         CppiaVar &var = *memberVars[i];
         if (var.name==inName)
         {
            outValue = var.setValue(inThis, inValue);
            return true;
         }
      }

      hx::FieldMap *map = dynamicMapOffset ? *(hx::FieldMap **)( (char *)inThis + dynamicMapOffset ) :
                           inThis->__GetFieldMap();
      if (map)
      {
         FieldMapSet(map, inName, inValue);
         outValue = inValue;
         return true;
      }

      // Fall though to haxe base
      //printf("Set field not found (%s) %s\n", inThis->toString().__s,inName.__s);

      return false;
   }


   int findFunctionSlot(int inName)
   {
      for(int i=0;i<memberFunctions.size();i++)
         if (memberFunctions[i]->nameId==inName)
            return memberFunctions[i]->vtableSlot;
      return -1;
   }

   ExprType findFunctionType(CppiaData &inData, int inName)
   {
      for(int i=0;i<memberFunctions.size();i++)
         if (memberFunctions[i]->nameId==inName)
            return inData.types[ memberFunctions[i]->returnType ]->expressionType;
      return etVoid;
   }


   CppiaVar *findVar(bool inStatic,int inId)
   {
      std::vector<CppiaVar *> &vars = inStatic ? staticVars : memberVars;
      for(int i=0;i<vars.size();i++)
      {
         if (vars[i]->nameId == inId)
            return vars[i];
      }

      std::vector<CppiaVar *> &dvars = inStatic ? staticDynamicFunctions : dynamicFunctions;
      for(int i=0;i<dvars.size();i++)
      {
         if (dvars[i]->nameId == inId)
            return dvars[i];
      }

      if (superType && superType->cppiaClass)
         return superType->cppiaClass->findVar(inStatic,inId);

      return 0;
   }

   void dumpVars(const char *inMessage, std::vector<CppiaVar *> &vars)
   {
      printf(" %s:\n", inMessage);
      for(int i=0;i<vars.size();i++)
         printf("   %d] %s (%d)\n", i, cppia.strings[ vars[i]->nameId ].__s, vars[i]->nameId );
   }

   void dumpFunctions(const char *inMessage, std::vector<CppiaFunction *> &funcs)
   {
      printf(" %s:\n", inMessage);
      for(int i=0;i<funcs.size();i++)
         printf("   %d] %s (%d)\n", i, cppia.strings[ funcs[i]->nameId ].__s, funcs[i]->nameId);
   }


   void dump()
   {
      printf("Class %s\n", name.c_str());
      dumpFunctions("Member functions",memberFunctions);
      dumpFunctions("Static functions",staticFunctions);
      dumpVars("Member vars",memberVars);
      dumpVars("Member dyns",dynamicFunctions);
      dumpVars("Static vars",staticVars);
      dumpVars("Static dyns",staticDynamicFunctions);
   }

   CppiaEnumConstructor *findEnum(int inFieldId)
   {
      for(int i=0;i<enumConstructors.size();i++)
         if (enumConstructors[i]->nameId==inFieldId)
            return enumConstructors[i];
      return 0;
   }

   void **getInterfaceVTable(const std::string &inName)
   {
      if (!interfaceVTables[inName] && superType->cppiaClass)
         return superType->cppiaClass->getInterfaceVTable(inName);
      return interfaceVTables[inName];
   }

   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(mClass);
      for(int i=0;i<enumConstructors.size();i++)
      {
         HX_MARK_MEMBER(enumConstructors[i]->value);
         HX_MARK_MEMBER(enumConstructors[i]->name);
      }
      for(int i=0;i<staticVars.size();i++)
         staticVars[i]->mark(__inCtx);
      for(int i=0;i<staticDynamicFunctions.size();i++)
         staticDynamicFunctions[i]->mark(__inCtx);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(mClass);
      for(int i=0;i<enumConstructors.size();i++)
      {
         HX_VISIT_MEMBER(enumConstructors[i]->value);
         HX_VISIT_MEMBER(enumConstructors[i]->name);
      }
      for(int i=0;i<staticVars.size();i++)
         staticVars[i]->visit(__inCtx);
      for(int i=0;i<staticDynamicFunctions.size();i++)
         staticDynamicFunctions[i]->visit(__inCtx);
   }



   bool load(CppiaStream &inStream)
   {
      std::string tok = inStream.getToken();
      isInterface = isEnum = false;

      if (tok=="CLASS")
         isInterface = false;
      else if (tok=="INTERFACE")
         isInterface = true;
      else if (tok=="ENUM")
         isEnum = true;
      else
         throw "Bad class type";

       typeId = inStream.getInt();
       mClass.mPtr = createCppiaClass(this);

       superId = isEnum ? 0 : inStream.getInt();
       int implementCount = isEnum ? 0 : inStream.getInt();
       implements.resize(implementCount);
       for(int i=0;i<implementCount;i++)
          implements[i] = inStream.getInt();

       name = cppia.typeStr(typeId);
       DBGLOG("Class %s %s\n", name.c_str(), isEnum ? "enum" : isInterface ? "interface" : "class" );
       bool isNew = Class_obj::Resolve( cppia.types[typeId]->name ) == null() &&
                    sScriptRegistered->find(name)==sScriptRegistered->end();

       if (isNew)
       {
          cppia.types[typeId]->cppiaClass = this;
       }
       else
       {
          DBGLOG(" -- IGNORE --\n");
       }

       inStream.cppiaData->creatingClass = name.c_str();

       int fields = inStream.getInt();
       for(int f=0;f<fields;f++)
       {
          if (isEnum)
          {
             CppiaEnumConstructor *enumConstructor = new CppiaEnumConstructor(cppia,inStream,this);
             enumConstructors.push_back( enumConstructor );
          }
          else
          {
             tok = inStream.getToken();
             if (tok=="FUNCTION")
             {
                bool isStatic = inStream.getStatic();
                bool isDynamic = inStream.getInt();
                CppiaFunction *func = new CppiaFunction(&cppia,isStatic,isDynamic);
                func->load(inStream,!isInterface);
                if (isDynamic)
                {
                   if (isStatic)
                      staticDynamicFunctions.push_back( new CppiaVar(func) );
                   else
                      dynamicFunctions.push_back( new CppiaVar(func) );
                }
                else
                {
                   if (isStatic)
                      staticFunctions.push_back(func);
                   else
                      memberFunctions.push_back(func);
                }
             }
             else if (tok=="VAR")
             {
                bool isStatic = inStream.getStatic();
                CppiaVar *var = new CppiaVar(isStatic);
                if (isStatic)
                   staticVars.push_back(var);
                else
                   memberVars.push_back(var);
                var->load(inStream);
             }
             else if (tok=="IMPLDYNAMIC")
             {
                // Fill in later
                dynamicMapOffset = -1;
             }
             else if (tok=="INLINE")
             {
                // OK
             }
             else
                throw "unknown field type";
          }
       }
       if (isEnum)
       {
          if (inStream.getBool())
             enumMeta = createCppiaExpr(inStream);
       }
       inStream.cppiaData->creatingClass = 0;
       return isNew;
   }

   void **createInterfaceVTable(int inTypeId)
   {
      std::vector<CppiaExpr *> vtable;

      ScriptRegisteredIntferface *interface = cppia.types[inTypeId]->interfaceBase;
      // Native-defined interface...
      if (interface)
      {
         vtable.push_back( findInterfaceFunction("toString") );
         ScriptNamedFunction *functions = interface->functions;
         for(ScriptNamedFunction *f = functions; f->name; f++)
            if (strcmp(f->name,"toString"))
               vtable.push_back( findInterfaceFunction(f->name) );
            
      }

      CppiaClassInfo *cls = cppia.types[inTypeId]->cppiaClass;
      if (!cls && !interface)
         throw "vtable for unknown class";

      if (cls && !cls->isInterface)
         throw "vtable for non-interface";

      if (cls)
      {
         for(int i=0;i<cls->memberFunctions.size();i++)
         {
            CppiaFunction *func = cls->memberFunctions[i];
            vtable.push_back( findFunction(false,func->nameId) );
         }
      }

      void **result = new void *[ vtable.size() + 1];
      result[0] = this;
      result++;
      memcpy(result, &vtable[0], sizeof(void *)*vtable.size());
      return result;
   }

   Class *getSuperClass()
   {
      DBGLOG("getSuperClass %s %d\n", name.c_str(), superId);
      if (!superId)
         return 0;

      TypeData *superType = cppia.types[ superId ];
      if (!superType)
         throw "Unknown super type!";
      if (superType->cppiaClass)
         return &superType->cppiaClass->mClass;
      return &superType->haxeClass;
   }


   void linkTypes()
   {
      if (isLinked)
         return;
      isLinked = true;

      type = cppia.types[typeId];
      mClass->mName = type->name;
      superType = superId ? cppia.types[ superId ] : 0;
      CppiaClassInfo  *cppiaSuper = superType ? superType->cppiaClass : 0;
      // Link super first
      if (superType && superType->cppiaClass)
         superType->cppiaClass->linkTypes();

      // implemented interfaces before main class
      for(int i=0;i<implements.size();i++)
         if (cppia.types[ implements[i] ]->cppiaClass)
            cppia.types[ implements[i] ]->cppiaClass->linkTypes();

      DBGLOG(" Linking class '%s' ", type->name.__s);
      if (!superType)
      {
         DBGLOG("script base\n");
      }
      else if (cppiaSuper)
      {
         DBGLOG("extends script '%s'\n", superType->name.__s);
      }
      else
      {
         DBGLOG("extends haxe '%s'\n", superType->name.__s);
      }

      // Link class before we combine the function list...
      linkCppiaClass(mClass.mPtr,cppia,type->name);


      haxeBase = type->haxeBase;
      if (!haxeBase && !isInterface)
         throw "No base defined for non-interface";

      classSize = haxeBase ? haxeBase->mDataOffset : 0;

      // Combine member vars ...
      if (cppiaSuper)
      {
         classSize = cppiaSuper->classSize;

         if (cppiaSuper->dynamicMapOffset!=0)
            dynamicMapOffset = cppiaSuper->dynamicMapOffset;

         std::vector<CppiaVar *> combinedVars(cppiaSuper->memberVars );
         for(int i=0;i<memberVars.size();i++)
         {
            for(int j=0;j<combinedVars.size();j++)
               if (combinedVars[j]->nameId==memberVars[i]->nameId)
                  printf("Warning duplicate member var %s\n", cppia.strings[memberVars[i]->nameId].__s);
            combinedVars.push_back(memberVars[i]);
         }
         memberVars.swap(combinedVars);

         std::vector<CppiaVar *> combinedDynamics(cppiaSuper->dynamicFunctions );
         for(int i=0;i<dynamicFunctions.size();i++)
         {
            bool found = false;
            for(int j=0;j<combinedDynamics.size() && !found;j++)
               if (dynamicFunctions[i]->nameId == combinedDynamics[j]->nameId)
               {
                  // Overwrite
                  dynamicFunctions[i]->offset = combinedDynamics[j]->offset;
                  combinedDynamics[j] = dynamicFunctions[i];
                  found = true;
               }
            if (!found)
               combinedDynamics.push_back(dynamicFunctions[i]);
         }
         dynamicFunctions.swap(combinedDynamics);

         // Combine member functions ...
         Functions combinedFunctions(cppiaSuper->memberFunctions );
         for(int i=0;i<memberFunctions.size();i++)
         {
            bool found = false;
            for(int j=0;j<combinedFunctions.size();j++)
               if (combinedFunctions[j]->name==memberFunctions[i]->name)
               {
                  combinedFunctions[j] = memberFunctions[i];
                  found = true;
                  break;
               }
            if (!found)
               combinedFunctions.push_back(memberFunctions[i]);
         }
         memberFunctions.swap(combinedFunctions);
      }


      // Non-interface classes will have haxeBase
      if (haxeBase)
      {
         // Calculate table offsets...
         DBGLOG("  base haxe size %s = %d\n", haxeBase->name.c_str(), classSize);
         for(int i=0;i<memberVars.size();i++)
         {
            if (memberVars[i]->offset)
            {
               DBGLOG("   super var %s @ %d\n", cppia.identStr(memberVars[i]->nameId), memberVars[i]->offset);
            }
            else
            {
               DBGLOG("   link var %s @ %d\n", cppia.identStr(memberVars[i]->nameId), classSize);
               memberVars[i]->linkVarTypes(cppia,classSize);
            }
         }
         for(int i=0;i<dynamicFunctions.size();i++)
         {
            if (dynamicFunctions[i]->offset)
            {
               DBGLOG("   super dynamic function %s @ %d\n", cppia.identStr(dynamicFunctions[i]->nameId), dynamicFunctions[i]->offset);
            }
            else
            {
               DBGLOG("   link dynamic function %s @ %d\n", cppia.identStr(dynamicFunctions[i]->nameId), classSize);
               dynamicFunctions[i]->linkVarTypes(cppia,classSize);
            }
         }

         if (dynamicMapOffset==-1)
         {
            dynamicMapOffset = classSize;
            classSize += sizeof( hx::FieldMap * );
         }

         extraData = classSize - haxeBase->mDataOffset;
      }


      DBGLOG("  script member vars size = %d\n", extraData);
 
      for(int i=0;i<staticVars.size();i++)
      {
         DBGLOG("   link static var %s\n", cppia.identStr(staticVars[i]->nameId));
         staticVars[i]->linkVarTypes(cppia);
      }

      for(int i=0;i<staticDynamicFunctions.size();i++)
      {
         DBGLOG("   link dynamic static var %s\n", cppia.identStr(staticDynamicFunctions[i]->nameId));
         staticDynamicFunctions[i]->linkVarTypes(cppia);
      }


      // Combine vtable positions...
      DBGLOG("  format haxe callable vtable (%lu)....\n", memberFunctions.size());
      std::vector<std::string> table;
      if (haxeBase)
         haxeBase->addVtableEntries(table);
      for(int i=0;i<table.size();i++)
         DBGLOG("   base table[%d] = %s\n", i, table[i].c_str() );

      int vtableSlot = table.size();
      for(int i=0;i<memberFunctions.size();i++)
      {
         int idx = -1;
         for(int j=0;j<table.size();j++)
            if (table[j] == memberFunctions[i]->name)
            {
               idx = j;
               break;
            }
         if (idx<0)
         {
            idx = vtableSlot++;
            DBGLOG("   cppia slot [%d] = %s\n", idx, memberFunctions[i]->name.c_str() );
         }
         else
            DBGLOG("   override slot [%d] = %s\n", idx, memberFunctions[i]->name.c_str() );
         memberFunctions[i]->setVTableSlot(idx);
      }
      vtable = new void*[vtableSlot + 1];
      memset(vtable, 0, sizeof(void *)*(vtableSlot+1));
      *vtable++ = this;
      DBGLOG("  vtable size %d -> %p\n", vtableSlot, vtable);

      // Create interface vtables...
      for(int i=0;i<implements.size();i++)
         interfaceVTables[ cppia.types[ implements[i] ]->name.__s ] = createInterfaceVTable( implements[i] );

      // Extract special function ...
      for(int i=0;i<staticFunctions.size(); )
      {
         if (staticFunctions[i]->name == "new")
         {
            newFunc = staticFunctions[i];
            staticFunctions.erase( staticFunctions.begin() + i);
         }
         else if (staticFunctions[i]->name == "__init__")
         {
            initFunc = staticFunctions[i]->funExpr;
            staticFunctions.erase( staticFunctions.begin() + i);
         }
         else
            i++;

      }

      for(int i=0;i<enumConstructors.size();i++)
      {
         CppiaEnumConstructor &e = *enumConstructors[i];
         e.name = cppia.strings[e.nameId];
         if (e.args.size()==0)
         {
            EnumBase base = new CppiaEnumBase(this);
            e.value = base;
            base->Set( cppia.strings[e.nameId],i,null() );
         }
         else
         {
            e.index = i;
            e.value = createEnumClosure(e);
         }
      }

      if (!newFunc && cppiaSuper && cppiaSuper->newFunc)
      {
         //throw "No chaining constructor";
         newFunc = cppiaSuper->newFunc;
      }

      DBGLOG("  this constructor %p\n", newFunc);
   }

   void link()
   {
      int newPos = -1;
      for(int i=0;i<staticFunctions.size();i++)
      {
         DBGLOG(" Link %s::%s\n", name.c_str(), staticFunctions[i]->name.c_str() );
         staticFunctions[i]->link();
      }
      if (newFunc)
         newFunc->link();

      if (initFunc)
         initFunc = initFunc->link(cppia);

      // Functions
      for(int i=0;i<memberFunctions.size();i++)
      {
         DBGLOG(" Link member %s::%s\n", name.c_str(), memberFunctions[i]->name.c_str() );
         memberFunctions[i]->link();
      }

      for(int i=0;i<staticDynamicFunctions.size();i++)
         staticDynamicFunctions[i]->link(cppia);

      for(int i=0;i<dynamicFunctions.size();i++)
         dynamicFunctions[i]->link(cppia);

      for(int i=0;i<memberFunctions.size();i++)
         vtable[ memberFunctions[i]->vtableSlot ] = memberFunctions[i]->funExpr;

      // Vars ...
      if (!isInterface)
      {
         for(int i=0;i<memberVars.size();i++)
         {
            CppiaVar &var = *memberVars[i];
            var.link(cppia);
            if (var.readAccess == CppiaVar::accCall)
            {
               CppiaExpr *getter = findFunction(false,HX_CSTRING("get_") + var.name);
               if (!getter)
               {
                  dump();
                  throw Dynamic(HX_CSTRING("Could not find getter for ") + var.name);
               }
               DBGLOG("  found getter for %s.%s\n", name.c_str(), var.name.__s);
               memberGetters[var.name.__s] = getter;
            }
            if (var.writeAccess == CppiaVar::accCall)
            {
               CppiaExpr *setter = findFunction(false,HX_CSTRING("set_") + var.name);
               if (!setter)
                  throw Dynamic(HX_CSTRING("Could not find setter for ") + var.name);
               DBGLOG("  found setter for %s.%s\n", name.c_str(), var.name.__s);
               memberSetters[var.name.__s] = setter;
            }
         }
      }
   
      for(int i=0;i<staticVars.size();i++)
      {
         CppiaVar &var = *staticVars[i];
         var.link(cppia);
         if (var.readAccess == CppiaVar::accCall)
         {
            CppiaExpr *getter = findFunction(true,HX_CSTRING("get_") + var.name);
            if (!getter)
               throw Dynamic(HX_CSTRING("Could not find getter for ") + var.name);
            DBGLOG("  found getter for %s.%s\n", name.c_str(), var.name.__s);
            staticGetters[var.name.__s] = getter;
         }
         if (var.writeAccess == CppiaVar::accCall)
         {
            CppiaExpr *setter = findFunction(true,HX_CSTRING("set_") + var.name);
            if (!setter)
               throw Dynamic(HX_CSTRING("Could not find setter for ") + var.name);
            DBGLOG("  found setter for %s.%s\n", name.c_str(), var.name.__s);
            staticSetters[var.name.__s] = setter;
         }

      }

      if (enumMeta)
         enumMeta = enumMeta->link(cppia);


      mClass->mStatics = GetClassFields();
      //printf("Found haxeBase %s = %p / %d\n", cppia.types[typeId]->name.__s, haxeBase, dataSize );
   }

   void init(CppiaCtx *ctx, int inPhase)
   {
      if (inPhase==0)
      {
         for(int i=0;i<staticVars.size();i++)
         {
            staticVars[i]->runInit(ctx);
            if (staticVars[i]->name==HX_CSTRING("__meta__"))
            {
               // Todo - delete/clean value
               mClass->__meta__ = staticVars[i]->objVal;
               staticVars.erase( staticVars.begin() + i );
               i--;
            }
         }
         if (enumMeta)
            mClass->__meta__ = enumMeta->runObject(ctx);

         for(int i=0;i<staticDynamicFunctions.size();i++)
            staticDynamicFunctions[i]->runInit(ctx);
      }
      else if (inPhase==1)
      {
         if (initFunc)
            initFunc->runVoid(ctx);
      }
   }


   Dynamic getStaticValue(const String &inName,bool inCallProp)
   {
      if (inCallProp)
      {
         CppiaExpr *getter = findStaticGetter(inName);
         if (getter)
         {
            Array<Dynamic> empty;
            return runFunExprDynamic(CppiaCtx::getCurrent(),getter,0, empty);
         }
      }

      CppiaExpr *closure = findFunction(true,inName);
      if (closure)
         return createMemberClosure(0,(ScriptCallable*)closure);

      // Look for dynamic function (variable)
      for(int i=0;i<staticDynamicFunctions.size();i++)
      {
         if (cppia.strings[ staticDynamicFunctions[i]->nameId  ]==inName)
         {
            CppiaVar *d = staticDynamicFunctions[i];

            return d->getStaticValue();
         }
      }

      for(int i=0;i<staticVars.size();i++)
      {
         CppiaVar &var = *staticVars[i];
         if (var.name==inName)
            return var.getStaticValue();
      }

      printf("Get static field not found (%s) %s\n", name.c_str(),inName.__s);
      return null();
   }

   Dynamic setStaticValue(const String &inName,const Dynamic &inValue ,bool inCallProp)
   {
      if (inCallProp)
      {
         CppiaExpr *setter = findStaticSetter(inName);
         if (setter)
         {
            Array<Dynamic> args = Array_obj<Dynamic>::__new(1,1);
            args[0] = inValue;
            return runFunExprDynamic(CppiaCtx::getCurrent(),setter,0, args);
         }
      }


      // Look for dynamic function (variable)
      for(int i=0;i<staticDynamicFunctions.size();i++)
      {
         if (cppia.strings[ staticDynamicFunctions[i]->nameId  ]==inName)
         {
            CppiaVar *d = staticDynamicFunctions[i];
            return d->setStaticValue(inValue);
         }
      }

      for(int i=0;i<staticVars.size();i++)
      {
         CppiaVar &var = *staticVars[i];
         if (var.name==inName)
         {
            return var.setStaticValue(inValue);
         }
      }

      printf("Set static field not found (%s) %s\n", name.c_str(), inName.__s);
      return null();

   }

   void GetInstanceFields(hx::Object *inObject, Array<String> &ioFields)
   {
      for(int i=0;i<memberVars.size();i++)
      {
         CppiaVar &var = *memberVars[i];
         ioFields->push(var.name);
      }

      if (inObject)
      {
         hx::FieldMap *map = inObject->__GetFieldMap();
         if (map)
            FieldMapAppendFields(map, ioFields);
      }
   }
   
   Array<String> GetClassFields()
   {
      Array<String> result = Array_obj<String>::__new();

      if (isEnum)
      {
         for(int i=0;i<enumConstructors.size();i++)
            result->push(enumConstructors[i]->name);
      }
      else
      {

         for(int i=0;i<staticVars.size();i++)
         {
            CppiaVar &var = *staticVars[i];
            if (var.isVirtual)
               continue;
            result->push(var.name);
         }

         for(int i=0;i<staticDynamicFunctions.size();i++)
         {
            CppiaVar &var = *staticDynamicFunctions[i];
            if (var.isVirtual)
               continue;
            result->push(var.name);
         }

         for(int i=0;i<staticFunctions.size();i++)
         {
            CppiaFunction &func = *staticFunctions[i];
            result->push(func.getName());
         }
      }

      return result;
   }


   inline void markInstance(hx::Object *inThis, hx::MarkContext *__inCtx)
   {
      if (dynamicMapOffset)
         hx::FieldMapMark( (*(hx::FieldMap **)( (char *)inThis + dynamicMapOffset )), __inCtx);

      for(int i=0;i<dynamicFunctions.size();i++)
         dynamicFunctions[i]->mark(inThis, __inCtx);
      // Only script members?
      for(int i=0;i<memberVars.size();i++)
         memberVars[i]->mark(inThis, __inCtx);
   }

   inline void visitInstance(hx::Object *inThis, hx::VisitContext *__inCtx)
   {
      if (dynamicMapOffset)
         hx::FieldMapVisit( ((hx::FieldMap **)( (char *)inThis + dynamicMapOffset )), __inCtx);

      for(int i=0;i<dynamicFunctions.size();i++)
         dynamicFunctions[i]->visit(inThis, __inCtx);
      for(int i=0;i<memberVars.size();i++)
         memberVars[i]->visit(inThis, __inCtx);
   }
};


void cppiaClassInit(CppiaClassInfo *inClass, CppiaCtx *ctx, int inPhase)
{
   inClass->init(ctx,inPhase);
}


void cppiaClassMark(CppiaClassInfo *inClass,hx::MarkContext *__inCtx)
{
   inClass->mark(__inCtx);
}
void cppiaClassVisit(CppiaClassInfo *inClass,hx::VisitContext *__inCtx)
{
   inClass->visit(__inCtx);
}


// --- Enum Base ---
::hx::ObjectPtr<Class_obj > CppiaEnumBase::__GetClass() const
{
   return classInfo->mClass;
}

::String CppiaEnumBase::GetEnumName( ) const
{
   return classInfo->mClass->mName;
}

::String CppiaEnumBase::__ToString() const
{
   return classInfo->mClass->mName + HX_CSTRING(".") + tag;
}







class CppiaClass : public Class_obj
{
public:
   CppiaClassInfo *info;

   CppiaClass(CppiaClassInfo *inInfo)
   {
      info = inInfo;
   }

   void linkClass(CppiaData &inData,String inName)
   {
      mName = inName;
      mSuper = info->getSuperClass();
      DBGLOG("LINK %p ########################### %s -> %p\n", this, mName.__s, mSuper );
      mStatics = Array_obj<String>::__new(0,0);


      Array<String> base;
      if (mSuper)
         base = (*mSuper)->mMembers;

      mMembers = base==null() ?Array_obj<String>::__new(0,0) : base->copy();

      for(int i=0;i<info->memberFunctions.size();i++)
      {
         CppiaFunction *func = info->memberFunctions[i];
         String name = func->getName();
         if (base==null() || base->Find(name)<0)
            mMembers->push(name);
      }

      for(int i=0;i<info->memberVars.size();i++)
      {
         CppiaVar *var = info->memberVars[i];
         if (var->isVirtual)
            continue;
         String name = inData.strings[var->nameId];
         if (base==null() || base->Find(name)<0)
            mMembers->push( name );
      }

      for(int i=0;i<info->dynamicFunctions.size();i++)
      {
         CppiaVar *var = info->dynamicFunctions[i];
         if (var->isVirtual)
            continue;
         String name = inData.strings[var->nameId];
         if (base==null() || base->Find(name)<0)
            mMembers->push(name);
      }

      registerScriptable(false);
   }


   Dynamic ConstructEmpty()
   {
      if (info->isEnum)
         return Dynamic();

      Expressions none;
      return info->createInstance(CppiaCtx::getCurrent(),none,false);
   } 

   Dynamic ConstructArgs(hx::DynamicArray inArgs)
   {
      return info->createInstance(CppiaCtx::getCurrent(),inArgs);
   }

   bool __IsEnum() { return info->isEnum; }

   Dynamic ConstructEnum(String inName,hx::DynamicArray inArgs)
   {
      if (!info->isEnum)
         return Dynamic();

      int index = info->getEnumIndex(inName);
	   return info->enumConstructors[index]->create(inArgs);
   }

   bool VCanCast(hx::Object *inPtr)
   {
      Class c = inPtr->__GetClass();
      if (!c.mPtr)
         return false;

      if (info->isInterface)
      {
         CppiaClass *cppiaClass = dynamic_cast<CppiaClass *>(c.mPtr);
         if (!cppiaClass)
            return false;
         return cppiaClass->info->implementsInterface(info);
      }


      while(c.mPtr)
      {
         if (c.mPtr==this)
            return true;
         if (info->isEnum)
            return false;
         c = c->GetSuper();
      }
      return false;
   }


   Dynamic __Field(const String &inName,bool inCallProp)
   {
      if (inName==HX_CSTRING("__meta__"))
         return __meta__;
      return info->getStaticValue(inName,inCallProp);
   }

   Dynamic __SetField(const String &inName,const Dynamic &inValue ,bool inCallProp)
   {
      return info->setStaticValue(inName,inValue,inCallProp);
   }

   bool __HasField(const String &inString)
   {
      printf("Static has field!\n");
      return null();
   }
};

Class_obj *createCppiaClass(CppiaClassInfo *inInfo) { return new CppiaClass(inInfo); }
void  linkCppiaClass(Class_obj *inClass, CppiaData &cppia, String inName)
{
   ((CppiaClass *)inClass)->linkClass(cppia,inName);
}


static String sInvalidArgCount = HX_CSTRING("Invalid arguement count");

struct ScriptCallable : public CppiaDynamicExpr
{
   int returnTypeId;
   ExprType returnType;
   int argCount;
   int stackSize;
   
   std::vector<CppiaStackVar> args;
   std::vector<bool>          hasDefault;
   std::vector<CppiaConst>    initVals;
   CppiaExpr *body;
   CppiaData *data;

   std::vector<CppiaStackVar *> captureVars;
   int                          captureSize;

   ScriptCallable(CppiaStream &stream)
   {
      body = 0;
      stackSize = 0;
      data = 0;
      returnTypeId = stream.getInt();
      argCount = stream.getInt();
      args.resize(argCount);
      hasDefault.resize(argCount);
      initVals.resize(argCount);
      captureSize = 0;
      for(int a=0;a<argCount;a++)
      {
         args[a].fromStream(stream);
         bool init = stream.getBool();
         hasDefault[a] = init;
         if (init)
            initVals[a].fromStream(stream);
      }
      body = createCppiaExpr(stream);
   }

   ScriptCallable(CppiaExpr *inBody) : CppiaDynamicExpr(inBody)
   {
      returnTypeId = 0;
      returnType = etVoid;
      argCount = 0;
      stackSize = 0;
      captureSize = 0;
      body = inBody;
   }

   CppiaExpr *link(CppiaData &inData)
   {
      StackLayout *oldLayout = inData.layout;
      StackLayout layout(oldLayout);
      inData.layout = &layout;
      data = &inData;

      returnType = inData.types[ returnTypeId ]->expressionType;
      layout.returnType = returnType;

      for(int a=0;a<args.size();a++)
         args[a].link(inData);

      body = body->link(inData);

      captureVars.swap(layout.captureVars);
      captureSize = layout.captureSize;

      stackSize = layout.size;
      inData.layout = oldLayout;
      return this;
   }

   ExprType getType() { return returnType; }

   void pushArgs(CppiaCtx *ctx, hx::Object *inThis, Expressions &inArgs)
   {
      int inCount = inArgs.size();
      bool badCount = argCount<inCount;

      for(int i=inCount;i<argCount && !badCount;i++)
         if (!hasDefault[i])
            badCount = true;

      if (badCount)
      {
         printf("Arg count mismatch %d!=%lu ?\n", argCount, inArgs.size());
         printf(" %s at %s:%d %s\n", getName(), filename, line, functionName);
         printf(" %s \n", inArgs[0]->runString(ctx).__s );
         CPPIA_CHECK(0);
         throw Dynamic(HX_CSTRING("Arg count error"));
         //return;
      }


      ctx->push( inThis );

      for(int a=0;a<argCount;a++)
      {
         CppiaStackVar &var = args[a];
         // TODO capture
         if (hasDefault[a])
         {
            bool makeNull = a>=inCount;
            hx::Object *obj = makeNull ? 0 : inArgs[a]->runObject(ctx);
            switch(var.expressionType)
            {
               case etInt:
                  ctx->pushInt( obj ? obj->__ToInt() : initVals[a].ival );
                  break;
               case etFloat:
                  ctx->pushFloat( (Float)(obj ? obj->__ToDouble() : initVals[a].dval) );
                  break;
               case etString:
                  ctx->push( obj ? obj->__ToString() : data->strings[ initVals[a].ival ] );
                  break;
               default:
                  if (obj)
                     ctx->pushObject(obj);
                  else
                  {
                     switch(initVals[a].type)
                     {
                        case CppiaConst::cInt:
                           ctx->pushObject( Dynamic(initVals[a].ival).mPtr );
                           break;
                        case CppiaConst::cFloat:
                           ctx->pushObject( Dynamic(initVals[a].dval).mPtr );
                           break;
                        case CppiaConst::cString:
                           ctx->pushObject( Dynamic(data->strings[ initVals[a].ival ]).mPtr );
                           break;
                        default:
                           ctx->pushObject(0);
                     }

                  }
            }
         }
         else
         {
            switch(var.expressionType)
            {
               case etInt:
                  ctx->pushInt(inArgs[a]->runInt(ctx));
                  break;
               case etFloat:
                  ctx->pushFloat(inArgs[a]->runFloat(ctx));
                  break;
               case etString:
                  ctx->pushString(inArgs[a]->runString(ctx));
                  break;
               default:
                  ctx->pushObject(inArgs[a]->runObject(ctx));
            }
         }
      }
   }



   void pushArgsDynamic(CppiaCtx *ctx, hx::Object *inThis, Array<Dynamic> &inArgs)
   {
      int inLen = inArgs==null() ? 0 : inArgs->length;
      if (argCount!=inLen)
      {
         printf("Arg count mismatch?\n");
         return;
      }

      ctx->push( inThis );

      for(int a=0;a<argCount;a++)
      {
         CppiaStackVar &var = args[a];
         // TODO capture
         if (hasDefault[a])
         {
            hx::Object *obj = inArgs[a].mPtr;
            switch(var.expressionType)
            {
               case etInt:
                  ctx->pushInt( obj ? obj->__ToInt() : initVals[a].ival );
                  break;
               case etFloat:
                  ctx->pushFloat( (Float)(obj ? obj->__ToDouble() : initVals[a].dval) );
                  break;
               case etString:
                  ctx->push( obj ? obj->toString() : data->strings[ initVals[a].ival ] );
                  break;
               default:
                  if (obj)
                     ctx->pushObject(obj);
                  else
                  {
                     switch(initVals[a].type)
                     {
                        case CppiaConst::cInt:
                           ctx->pushObject( Dynamic(initVals[a].ival).mPtr );
                           break;
                        case CppiaConst::cFloat:
                           ctx->pushObject( Dynamic(initVals[a].dval).mPtr );
                           break;
                        case CppiaConst::cString:
                           ctx->pushObject( Dynamic(data->strings[ initVals[a].ival ]).mPtr );
                           break;
                        default:
                           ctx->pushObject(0);
                     }

                  }
            }
         }
         else
         {
            switch(var.expressionType)
            {
               case etInt:
                  ctx->pushInt(inArgs[a]);
                  break;
               case etFloat:
                  ctx->pushFloat(inArgs[a]);
                  break;
               case etString:
                  ctx->pushString(inArgs[a]);
                  break;
               default:
                  ctx->pushObject(inArgs[a].mPtr);
            }
         }
      }
   }





   // Return the closure
   hx::Object *runObject(CppiaCtx *ctx)
   {
      return createClosure(ctx,this);

   }

   const char *getName() { return "ScriptCallable"; }
   String runString(CppiaCtx *ctx) { return HX_CSTRING("#function"); }

   // Run the actual function
   void runVoid(CppiaCtx *ctx)
   {
      if (stackSize)
      {
         memset(ctx->pointer, 0 , stackSize );
         ctx->pointer += stackSize;
      }
      body->runVoid(ctx);
   }

   void addStackVarsSpace(CppiaCtx *ctx)
   {
      if (stackSize)
      {
         memset(ctx->pointer, 0 , stackSize );
         ctx->pointer += stackSize;
      }
   }

   
   bool pushDefault(CppiaCtx *ctx,int arg)
   {
      if (!hasDefault[arg])
         return false;

      switch(args[arg].expressionType)
      {
         case etInt:
            ctx->pushInt( initVals[arg].ival );
            break;
         case etFloat:
            ctx->pushFloat( initVals[arg].dval );
            break;
         case etString:
            ctx->pushString( data->strings[initVals[arg].ival] );
            break;
         default:
            switch(initVals[arg].type)
            {
               case CppiaConst::cInt:
                  ctx->pushObject( Dynamic(initVals[arg].ival).mPtr );
                  break;
               case CppiaConst::cFloat:
                  ctx->pushObject( Dynamic(initVals[arg].dval).mPtr );
                  break;
               case CppiaConst::cString:
                  ctx->pushObject( Dynamic(data->strings[ initVals[arg].ival ]).mPtr );
                  break;
               default:
                  ctx->pushObject(0);
            }

      }
      return true;
   }

   void addExtraDefaults(CppiaCtx *ctx,int inHave)
   {
      if (inHave>argCount)
         throw sInvalidArgCount;

      for(int a=inHave;a<argCount;a++)
      {
         CppiaStackVar &var = args[a];
         if (!pushDefault(ctx,a))
            throw sInvalidArgCount;
      }
   }

};

CppiaExpr *convertToFunction(CppiaExpr *inExpr) { return new ScriptCallable(inExpr); }



class CppiaClosure : public hx::Object
{
public:
   inline void *operator new( size_t inSize, int inExtraDataSize )
     { return hx::InternalNew(inSize + inExtraDataSize,true); }
   inline void operator delete(void *,int) {}

   ScriptCallable *function;

   CppiaClosure(CppiaCtx *ctx, ScriptCallable *inFunction)
   {
      function = inFunction;

      unsigned char *base = ((unsigned char *)this) + sizeof(CppiaClosure);

      *(hx::Object **)base = ctx->getThis();

      for(int i=0;i<function->captureVars.size();i++)
      {
         CppiaStackVar *var = function->captureVars[i];
         int size = sTypeSize[var->expressionType];
         memcpy( base+var->capturePos, ctx->frame + var->fromStackPos, size );
      }
   }

   hx::Object **getThis() const
   {
      unsigned char *base = ((unsigned char *)this) + sizeof(CppiaClosure);
      return (hx::Object **)base;
   }

   // Create member closure...
   CppiaClosure(hx::Object *inThis, ScriptCallable *inFunction)
   {
      function = inFunction;
      *getThis() = inThis;
   }


   Dynamic doRun(CppiaCtx *ctx, int inHaveArgs)
   {
      function->addExtraDefaults(ctx,inHaveArgs);
      function->addStackVarsSpace(ctx);

      unsigned char *base = ((unsigned char *)this) + sizeof(CppiaClosure);
      *(hx::Object **)ctx->frame =  *(hx::Object **)base;

      for(int i=0;i<function->captureVars.size();i++)
      {
         CppiaStackVar *var = function->captureVars[i];
         int size = sTypeSize[var->expressionType];
         memcpy( ctx->frame+var->stackPos, base + var->capturePos, size);
      }

      switch(function->returnType)
      {
         case etFloat:
            return ctx->runFloat( function->body );
         case etInt:
            return ctx->runInt( function->body );
         case etString:
            return ctx->runString( function->body );
         case etObject:
            return ctx->runObject( function->body );
         default: break;
      }
      ctx->runVoid( function->body );
      return null();
   }

   void pushArg(CppiaCtx *ctx, int a, Dynamic inValue)
   {
      if (!inValue.mPtr && function->pushDefault(ctx,a) )
          return;

      switch(function->args[a].expressionType)
      {
         case etInt:
            ctx->pushInt(inValue->__ToInt());
            return;
         case etFloat:
            ctx->pushFloat(inValue->__ToDouble());
            break;
         case etString:
            ctx->pushString(inValue->toString());
            break;
         default:
            ctx->pushObject(inValue.mPtr);
      }
   }



   Dynamic __Run(const Array<Dynamic> &inArgs)
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();

      AutoStack a(ctx);
      ctx->pointer += sizeof(hx::Object *);

      int haveArgs = inArgs->length;
      if (haveArgs>function->argCount)
         throw sInvalidArgCount;

      for(int a=0; a<haveArgs; a++)
         pushArg(ctx,a,inArgs[a]);

      return doRun(ctx,haveArgs);
   }

   Dynamic __run()
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();
      AutoStack a(ctx);
      ctx->pointer += sizeof(hx::Object *);
      return doRun(ctx,0);
   }

   Dynamic __run(D a)
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();
      AutoStack aut(ctx);
      ctx->pointer += sizeof(hx::Object *);
      pushArg(ctx,0,a);
      return doRun(ctx,1);
   }
   Dynamic __run(D a,D b)
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();
      AutoStack aut(ctx);
      ctx->pointer += sizeof(hx::Object *);
      pushArg(ctx,0,a);
      pushArg(ctx,1,b);
      return doRun(ctx,2);
   }
   Dynamic __run(D a,D b,D c)
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();
      AutoStack aut(ctx);
      ctx->pointer += sizeof(hx::Object *);
      pushArg(ctx,0,a);
      pushArg(ctx,1,b);
      pushArg(ctx,2,c);
      return doRun(ctx,3);
   }
   Dynamic __run(D a,D b,D c,D d)
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();
      AutoStack aut(ctx);
      ctx->pointer += sizeof(hx::Object *);
      pushArg(ctx,0,a);
      pushArg(ctx,1,b);
      pushArg(ctx,2,c);
      pushArg(ctx,3,d);
      return doRun(ctx,4);

   }
   Dynamic __run(D a,D b,D c,D d,D e)
   {
      CppiaCtx *ctx = CppiaCtx::getCurrent();
      AutoStack aut(ctx);
      ctx->pointer += sizeof(hx::Object *);
      pushArg(ctx,0,a);
      pushArg(ctx,1,b);
      pushArg(ctx,2,c);
      pushArg(ctx,3,d);
      pushArg(ctx,4,e);
      return doRun(ctx,5);
   }

   void __Mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(*getThis());
      for(int i=0;i<function->captureVars.size();i++)
         function->captureVars[i]->markClosure((char *)this,__inCtx);
   }
   void __Visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(*getThis());
      for(int i=0;i<function->captureVars.size();i++)
         function->captureVars[i]->visitClosure((char *)this,__inCtx);
   }
   virtual void *__GetHandle() const { return *getThis(); }

   int __Compare(const hx::Object *inRHS) const
   {
      const CppiaClosure *other = dynamic_cast<const CppiaClosure *>(inRHS);
      if (!other)
         return -1;
      return (function==other->function && *getThis()==*other->getThis())? 0 : -1;
   }



   int __GetType() const { return vtFunction; }
   int __ArgCount() const { return function->args.size(); }

   String toString() { return HX_CSTRING("function"); }
};


hx::Object *createClosure(CppiaCtx *ctx, ScriptCallable *inFunction)
{
   return new (inFunction->captureSize) CppiaClosure(ctx,inFunction);
}

hx::Object *createMemberClosure(hx::Object *inThis, ScriptCallable *inFunction)
{
   return new (sizeof(hx::Object *)) CppiaClosure(inThis,inFunction);
}


class EnumConstructorClosure : public hx::Object
{
public:
   CppiaEnumConstructor &constructor;

   EnumConstructorClosure(CppiaEnumConstructor &c) : constructor(c)
   {
   }

   Dynamic __Run(const Array<Dynamic> &inArgs)
   {
      return constructor.create(inArgs);
   }

   int __GetType() const { return vtFunction; }
   int __ArgCount() const { return constructor.args.size(); }

   String toString() { return HX_CSTRING("#function(") + constructor.name + HX_CSTRING(")"); }
};



hx::Object *createEnumClosure(CppiaEnumConstructor &inContructor)
{
   return new EnumConstructorClosure(inContructor);
}



void CppiaData::where(CppiaExpr *e)
{
   if (linkingClass && layout)
      printf(" in %s::%p\n", linkingClass->name.c_str(), layout);
   printf("   %s at %s:%d %s\n", e->getName(), e->filename, e->line, e->functionName);
}




void runFunExpr(CppiaCtx *ctx, CppiaExpr *inFunExpr, hx::Object *inThis, Expressions &inArgs )
{
   unsigned char *pointer = ctx->pointer;
   ((ScriptCallable *)inFunExpr)->pushArgs(ctx, inThis, inArgs);
   AutoStack save(ctx,pointer);
   ctx->runVoid( inFunExpr );
}


hx::Object *runFunExprDynamic(CppiaCtx *ctx, CppiaExpr *inFunExpr, hx::Object *inThis, Array<Dynamic> &inArgs )
{

   unsigned char *pointer = ctx->pointer;
   ((ScriptCallable *)inFunExpr)->pushArgsDynamic(ctx, inThis, inArgs);
   AutoStack save(ctx,pointer);
   return runContextConvertObject(ctx, inFunExpr->getType(), inFunExpr );
}


void runFunExprDynamicVoid(CppiaCtx *ctx, CppiaExpr *inFunExpr, hx::Object *inThis, Array<Dynamic> &inArgs )
{

   unsigned char *pointer = ctx->pointer;
   ((ScriptCallable *)inFunExpr)->pushArgsDynamic(ctx, inThis, inArgs);
   AutoStack save(ctx,pointer);
   ctx->runVoid(inFunExpr);
}



struct BlockCallable : public ScriptCallable
{
   BlockCallable(CppiaExpr *inExpr) : ScriptCallable(inExpr)
   {
   }

   ExprType getType() { return body->getType(); }

   void runVoid(CppiaCtx *ctx)
   {
      unsigned char *pointer = ctx->pointer;
      ctx->push( ctx->getThis() );
      AutoStack save(ctx,pointer);
      addStackVarsSpace(ctx);
      CPPIA_STACK_FRAME(this);
      body->runVoid(ctx);
   }
   int runInt(CppiaCtx *ctx)
   {
      unsigned char *pointer = ctx->pointer;
      ctx->push( ctx->getThis() );
      AutoStack save(ctx,pointer);
      addStackVarsSpace(ctx);
      CPPIA_STACK_FRAME(this);
      return body->runInt(ctx);
   }
   Float runFloat(CppiaCtx *ctx)
   {
      unsigned char *pointer = ctx->pointer;
      ctx->push( ctx->getThis() );
      AutoStack save(ctx,pointer);
      addStackVarsSpace(ctx);
      CPPIA_STACK_FRAME(this);
      return body->runFloat(ctx);
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      unsigned char *pointer = ctx->pointer;
      ctx->push( ctx->getThis() );
      AutoStack save(ctx,pointer);
      addStackVarsSpace(ctx);
      CPPIA_STACK_FRAME(this);
      return body->runObject(ctx);
   }
};


struct BlockExpr : public CppiaExpr
{
   Expressions expressions;

   BlockExpr(CppiaStream &stream)
   {
      ReadExpressions(expressions,stream);
   }

   CppiaExpr *link(CppiaData &data)
   {
      if (data.layout==0)
      {
         CppiaExpr *blockFunc = new BlockCallable(this);
         blockFunc->link(data);
         return blockFunc;
      }



      LinkExpressions(expressions,data);
      return this;
   }

   const char *getName() { return "BlockExpr"; }
   virtual ExprType getType()
   {
      if (expressions.size()==0)
         return etNull;
      return expressions[expressions.size()-1]->getType();
   }

   #define BlockExprRun(ret,name,defVal) \
     ret name(CppiaCtx *ctx) \
     { \
        for(int a=0;a<expressions.size()-1;a++) \
        { \
          CPPIA_STACK_LINE(expressions[a]); \
          expressions[a]->runVoid(ctx); \
        } \
        if (expressions.size()>0) \
           return expressions[expressions.size()-1]->name(ctx); \
        return defVal; \
     }
   BlockExprRun(int,runInt,0)
   BlockExprRun(Float ,runFloat,0)
   BlockExprRun(String,runString,null())
   BlockExprRun(hx::Object *,runObject,0)
   void  runVoid(CppiaCtx *ctx)
   {
      for(int a=0;a<expressions.size();a++)
      {
         CPPIA_STACK_LINE(expressions[a]);
         expressions[a]->runVoid(ctx);
         #ifdef FLAG_BREAK
         BCR_VCHECK;
         #endif
      }
   }
};

struct IfElseExpr : public CppiaExpr
{
   CppiaExpr *condition;
   CppiaExpr *doIf;
   CppiaExpr *doElse;

   const char *getName() { return "IfElseExpr"; }
   IfElseExpr(CppiaStream &stream)
   {
      condition = createCppiaExpr(stream);
      doIf = createCppiaExpr(stream);
      doElse = createCppiaExpr(stream);
   }

   CppiaExpr *link(CppiaData &inData)
   {
      condition = condition->link(inData);
      doIf = doIf->link(inData);
      doElse = doElse->link(inData);
      return this;
   }

   void runVoid(CppiaCtx *ctx)
   {
      if (condition->runInt(ctx))
         doIf->runVoid(ctx);
      else
         doElse->runVoid(ctx);
   }
   #define IF_ELSE_RUN(TYPE,NAME) \
   TYPE NAME(CppiaCtx *ctx) \
   { \
      if (condition->runInt(ctx)) \
         return doIf->NAME(ctx); \
      return doElse->NAME(ctx); \
   }
   IF_ELSE_RUN(hx::Object *,runObject)
   IF_ELSE_RUN(int,runInt)
   IF_ELSE_RUN(String,runString)
   IF_ELSE_RUN(Float,runFloat)
};


struct IfExpr : public CppiaDynamicExpr
{
   CppiaExpr *condition;
   CppiaExpr *doIf;

   IfExpr(CppiaStream &stream)
   {
      condition = createCppiaExpr(stream);
      doIf = createCppiaExpr(stream);
   }

   const char *getName() { return "IfExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      condition = condition->link(inData);
      doIf = doIf->link(inData);
      return this;
   }

   hx::Object *runObject(CppiaCtx *ctx) { runVoid(ctx); return 0; }
   void runVoid(CppiaCtx *ctx)
   {
      if (condition->runInt(ctx))
         doIf->runVoid(ctx);
   }
};


struct IsNull : public CppiaBoolExpr
{
   CppiaExpr *condition;

   IsNull(CppiaStream &stream) { condition = createCppiaExpr(stream); }

   const char *getName() { return "IsNull"; }

   CppiaExpr *link(CppiaData &inData)
   {
      condition = condition->link(inData);
      return this;
   }

   int runInt(CppiaCtx *ctx)
   {
      return condition->runObject(ctx)==0;
   }

};


struct IsNotNull : public CppiaBoolExpr
{
   CppiaExpr *condition;

   IsNotNull(CppiaStream &stream) { condition = createCppiaExpr(stream); }

   const char *getName() { return "IsNotNull"; }
   CppiaExpr *link(CppiaData &inData) { condition = condition->link(inData); return this; }

   int runInt(CppiaCtx *ctx)
   {
      return condition->runObject(ctx)!=0;
   }
};


struct CallFunExpr : public CppiaExpr
{
   Expressions args;
   CppiaExpr   *thisExpr;
   ScriptCallable     *function;
   ExprType    returnType;

   CallFunExpr(const CppiaExpr *inSrc, CppiaExpr *inThisExpr, ScriptCallable *inFunction, Expressions &ioArgs )
      : CppiaExpr(inSrc)
   {
      args.swap(ioArgs);
      function = inFunction;
      thisExpr = inThisExpr;
   }

   CppiaExpr *link(CppiaData &inData)
   {
      LinkExpressions(args,inData);
      // Should already be linked
      //function = (ScriptCallable *)function->link(inData);
      if (thisExpr)
         thisExpr = thisExpr->link(inData);
      returnType = inData.types[ function->returnType ]->expressionType;
      return this;
   }

   const char *getName() { return "CallFunExpr"; }
   ExprType getType() { return returnType; }

   #define CallFunExprVal(ret,name,funcName) \
   ret name(CppiaCtx *ctx) \
   { \
      unsigned char *pointer = ctx->pointer; \
      function->pushArgs(ctx,thisExpr?thisExpr->runObject(ctx):0,args); \
      AutoStack save(ctx,pointer); \
      return funcName(ctx, function->getType(), function); \
   }
   CallFunExprVal(int,runInt, runContextConvertInt);
   CallFunExprVal(Float ,runFloat, runContextConvertFloat);
   CallFunExprVal(String,runString, runContextConvertString);
   CallFunExprVal(hx::Object *,runObject,  runContextConvertObject);


   void runVoid(CppiaCtx *ctx)
   {
      unsigned char *pointer = ctx->pointer;
      function->pushArgs(ctx,thisExpr?thisExpr->runObject(ctx):ctx->getThis(),args);
      AutoStack save(ctx,pointer);
      ctx->runVoid(function);
   }

};

// ---


struct CppiaExprWithValue : public CppiaDynamicExpr
{
   Dynamic     value;

   CppiaExprWithValue(const CppiaExpr *inSrc=0) : CppiaDynamicExpr(inSrc)
   {
      value.mPtr = 0;
   }

   hx::Object *runObject(CppiaCtx *ctx) { return value.mPtr; }
   void mark(hx::MarkContext *__inCtx) { HX_MARK_MEMBER(value); }
   void visit(hx::VisitContext *__inCtx) { HX_VISIT_MEMBER(value); }

   const char *getName() { return "CppiaExprWithValue"; }
   CppiaExpr *link(CppiaData &inData)
   {
      inData.markable.push_back(this);
      return this;
   }

   void runVoid(CppiaCtx *ctx) { runObject(ctx); }

};

// ---



struct CallDynamicFunction : public CppiaExprWithValue
{
   Expressions args;

   CallDynamicFunction(CppiaData &inData, const CppiaExpr *inSrc,
                       Dynamic inFunction, Expressions &ioArgs )
      : CppiaExprWithValue(inSrc)
   {
      args.swap(ioArgs);
      value = inFunction;
      inData.markable.push_back(this);
   }

   CppiaExpr *link(CppiaData &inData)
   {
      LinkExpressions(args,inData);
      return CppiaExprWithValue::link(inData);
   }

   const char *getName() { return "CallDynamicFunction"; }
   ExprType getType() { return etObject; }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      int n = args.size();
      switch(n)
      {
         case 0:
            return value->__run().mPtr;
         case 1:
            {
               Dynamic arg0( args[0]->runObject(ctx) );
               return value->__run(arg0).mPtr;
            }
         case 2:
            {
               Dynamic arg0( args[0]->runObject(ctx) );
               Dynamic arg1( args[1]->runObject(ctx) );
               return value->__run(arg0,arg1).mPtr;
            }
         case 3:
            {
               Dynamic arg0( args[0]->runObject(ctx) );
               Dynamic arg1( args[1]->runObject(ctx) );
               Dynamic arg2( args[2]->runObject(ctx) );
               return value->__run(arg0,arg1,arg2).mPtr;
            }
         case 4:
            {
               Dynamic arg0( args[0]->runObject(ctx) );
               Dynamic arg1( args[1]->runObject(ctx) );
               Dynamic arg2( args[2]->runObject(ctx) );
               Dynamic arg3( args[3]->runObject(ctx) );
               return value->__run(arg0,arg1,arg2,arg3).mPtr;
            }
         case 5:
            {
               Dynamic arg0( args[0]->runObject(ctx) );
               Dynamic arg1( args[1]->runObject(ctx) );
               Dynamic arg2( args[2]->runObject(ctx) );
               Dynamic arg3( args[3]->runObject(ctx) );
               Dynamic arg4( args[4]->runObject(ctx) );
               return value->__run(arg0,arg1,arg2,arg3,arg4).mPtr;
            }
      }

      Array<Dynamic> argVals = Array_obj<Dynamic>::__new(n,n);
      for(int a=0;a<n;a++)
         argVals[a] = Dynamic( args[a]->runObject(ctx) );
      return value->__Run(argVals).mPtr;
   }

   int runInt(CppiaCtx *ctx)
   {
      hx::Object *result = runObject(ctx);
      return result ? result->__ToInt() : 0;
   }
   Float  runFloat(CppiaCtx *ctx)
   {
      hx::Object *result = runObject(ctx);
      return result ? result->__ToDouble() : 0;
   }
   String runString(CppiaCtx *ctx)
   {
      hx::Object *result = runObject(ctx);
      return result ? result->toString() : String();
   }
};

struct SetExpr : public CppiaExpr
{
   CppiaExpr *lvalue;
   CppiaExpr *value;
   AssignOp op;

   SetExpr(CppiaStream &stream,AssignOp inOp)
   {
      op = inOp;
      lvalue = createCppiaExpr(stream);
      value = createCppiaExpr(stream);
   }

   const char *getName() { return "SetExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      lvalue = lvalue->link(inData);
      value = value->link(inData);
      CppiaExpr *result = lvalue->makeSetter(op,value);
      if (!result)
      {
         printf("Could not makeSetter.\n");
         inData.where(lvalue);
         throw "Bad Set expr";
      }
      delete this;
      return result;
   }

};

class CppiaInterface : public hx::Interface
{
   typedef CppiaInterface __ME;
   typedef hx::Interface super;
   HX_DEFINE_SCRIPTABLE_INTERFACE
};

enum CastOp
{
   castNOP,
   castDataArray,
   castDynArray,
};

struct CastExpr : public CppiaDynamicExpr
{
   CppiaExpr *value;
   CastOp  op;
   int     typeId;
   ArrayType arrayType;

   CastExpr(CppiaStream &stream,CastOp inOp)
   {
      op = inOp;
      typeId = 0;
      if (op==castDataArray)
         typeId = stream.getInt();

      value = createCppiaExpr(stream);
   }

   template<typename T>
   hx::Object *convert(hx::Object *obj)
   {
      Array_obj<T> *alreadyGood = dynamic_cast<Array_obj<T> *>(obj);
      if (alreadyGood)
         return alreadyGood;
      int n = obj->__length();
      Array<T> result = Array_obj<T>::__new(n,n);
      for(int i=0;i<n;i++)
         result[i] = obj->__GetItem(i);
      return result.mPtr;
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = value->runObject(ctx);
      if (!obj)
         return 0;

      switch(arrayType)
      {
         case arrBool:         return convert<bool>(obj);
         case arrUnsignedChar: return convert<unsigned char>(obj);
         case arrInt:          return convert<int>(obj);
         case arrFloat:        return convert<Float>(obj);
         case arrString:       return convert<String>(obj);
         case arrAny:          return convert<Dynamic>(obj);
         case arrObject:       return obj;
         case arrNotArray:     throw "Bad cast";
      }
      return 0;
   }

   const char *getName() { return "CastExpr"; }

   CppiaExpr *link(CppiaData &inData)
   {
      value = value->link(inData);

      if (op==castNOP)
      {
         CppiaExpr *replace = value;
         delete this;
         return replace;
      }
      if (op==castDataArray)
      {
         TypeData *t = inData.types[typeId];
         arrayType = t->arrayType;
         if (arrayType==arrNotArray)
         {
            printf("Cast to %d, %s\n", typeId, t->name.__s);
            throw "Data cast to non-array";
         }
      }
      else
         arrayType = arrObject;
      return this;
   }
};

struct ToInterface : public CppiaDynamicExpr
{
   int       fromTypeId;
   int       toTypeId;
   CppiaExpr *value;
   bool      useNative;
   bool      array;
   ScriptRegisteredIntferface *interfaceInfo;
   void      **cppiaVTable;
   TypeData *toType;


   ToInterface(CppiaStream &stream,bool inArray)
   {
      toTypeId = stream.getInt();
      array = inArray;
      fromTypeId = array ? 0 : stream.getInt();
      value = createCppiaExpr(stream);
      interfaceInfo = 0;
      useNative = false;
      cppiaVTable = 0;
      toType = 0;
   }

   const char *getName() { return array ? "ToInterfaceArray" : "ToInterface"; }

   CppiaExpr *link(CppiaData &inData)
   {
      toType = inData.types[toTypeId];
      TypeData *fromType = fromTypeId ? inData.types[fromTypeId] : 0;

      if (toType->interfaceBase)
      {
         interfaceInfo = toType->interfaceBase;
         if (!fromType)
         {
            useNative = true;
         }
         else if (!fromType->cppiaClass)
         {
            DBGLOG("native -> native\n");
            useNative = true;
         }
         else
         {
            DBGLOG("cppa class, native interface\n");
            cppiaVTable = fromType->cppiaClass->getInterfaceVTable(toType->interfaceBase->name);
         }
      }
      else if (fromType && fromType->cppiaClass)
      {
         cppiaVTable = fromType->cppiaClass->getInterfaceVTable(toType->name.__s);
         if (!cppiaVTable)
           printf("Could not find scripting interface implementation %s on %s\n", toType->name.__s, fromType->name.__s);
      }
      else
      {
         // Use dynamic
      }
      value = value->link(inData);
      return this;
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = value->runObject(ctx);
      if (!obj && !array)
         return 0;

      if (interfaceInfo)
      {
         if (cppiaVTable)
         {
            if (array)
            {
               CPPIA_CHECK(obj);
               int n = obj->__length();
               Array<Dynamic> result = Array_obj<Dynamic>::__new(n,n);
               for(int i=0;i<n;i++)
                  result[i] = interfaceInfo->factory(cppiaVTable,obj->__GetItem(i)->__GetRealObject());
               return result.mPtr;
            }
            return interfaceInfo->factory(cppiaVTable,obj->__GetRealObject());
         }
         return obj->__ToInterface(*interfaceInfo->mType);
      }

      if (cppiaVTable)
      {
         if (array)
         {
            CPPIA_CHECK(obj);
            int n = obj->__length();
            Array<Dynamic> result = Array_obj<Dynamic>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = CppiaInterface::__script_create(cppiaVTable,obj->__GetItem(i)->__GetRealObject());
            return result.mPtr;
         }

         return CppiaInterface::__script_create(cppiaVTable,obj);
      }

      if (array)
      {
         CPPIA_CHECK(obj);
         int n = obj->__length();
         Array<Dynamic> result = Array_obj<Dynamic>::__new(n,n);
         for(int i=0;i<n;i++)
         {
            Dynamic o = obj->__GetItem(i)->__GetRealObject();
            if (o.mPtr)
            {
               void **vtable = o->__GetScriptVTable();
               if (vtable)
               {
                  CppiaClassInfo *info = (CppiaClassInfo *)vtable[-1];
                  void **interfaceVTable = info->getInterfaceVTable(toType->name.__s);
                  if (interfaceVTable)
                     result[i] =  CppiaInterface::__script_create(interfaceVTable,o.mPtr);
               }
            }
         }
         return result.mPtr;
      }

      void **vtable = obj->__GetScriptVTable();
      if (vtable)
      {
         CppiaClassInfo *info = (CppiaClassInfo *)vtable[-1];
         void **interfaceVTable = info->getInterfaceVTable(toType->name.__s);
         if (!interfaceVTable)
            return 0;
         return CppiaInterface::__script_create(interfaceVTable,obj);
      }

      return 0;
   }
};

struct NewExpr : public CppiaDynamicExpr
{
   int classId;
   TypeData *type;
   Expressions args;
	hx::ConstructArgsFunc constructor;


   NewExpr(CppiaStream &stream)
   {
      classId = stream.getInt();
      constructor = 0;
      ReadExpressions(args,stream);
   }

   const char *getName() { return "NewExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      type = inData.types[classId];
      if (!type->cppiaClass && type->haxeClass.mPtr)
         constructor = type->haxeClass.mPtr->mConstructArgs;

      LinkExpressions(args,inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      if (type->arrayType)
      {
         int size = 0;
         if (args.size()==1)
            size = args[0]->runInt(ctx);
         switch(type->arrayType)
         {
            case arrBool:
               return Array_obj<bool>::__new(size,size).mPtr;
            case arrUnsignedChar:
               return Array_obj<unsigned char>::__new(size,size).mPtr;
            case arrInt:
               return Array_obj<int>::__new(size,size).mPtr;
            case arrFloat:
               return Array_obj<Float>::__new(size,size).mPtr;
            case arrString:
               return Array_obj<String>::__new(size,size).mPtr;
            case arrObject:
            case arrAny:
               return Array_obj<Dynamic>::__new(size,size).mPtr;
            default:
               return 0;
         }
      }
      else if (constructor)
      {
         int n = args.size();
         Array< Dynamic > argList = Array_obj<Dynamic>::__new(n,n);
         for(int a=0;a<n;a++)
            argList[a].mPtr = args[a]->runObject(ctx);
            
          return constructor(argList).mPtr;
      }
      else
      {
         return type->cppiaClass->createInstance(ctx,args);
      }

      printf("Can't create non haxe type\n");
      return 0;
   }
   
};

template<typename T>
inline void SetVal(null &out, const T &value) {  }
template<typename T>
inline void SetVal(int &out, const T &value) { out = value; }
inline void SetVal(int &out, const String &value) { out = 0; }
template<typename T>
inline void SetVal(Float &out, const T &value) { out = value; }
inline void SetVal(Float &out, const String &value) { out = 0; }
template<typename T>
inline void SetVal(String &out, const T &value) { out = String(value); }
template<typename T>
inline void SetVal(Dynamic &out, const T &value) { out = value; }

//template<typname RETURN>
struct CallHaxe : public CppiaExpr
{
   Expressions args;
   CppiaExpr *thisExpr;
   ScriptFunction function;
   ExprType returnType;

   CallHaxe(CppiaExpr *inSrc,ScriptFunction inFunction, CppiaExpr *inThis, Expressions &ioArgs )
       : CppiaExpr(inSrc)
   {
      args.swap(ioArgs);
      thisExpr = inThis;
      function = inFunction;
   }
   ExprType getType() { return returnType; }
   CppiaExpr *link(CppiaData &inData)
   {
      if (strlen(function.signature) != args.size()+1)
         throw "CallHaxe: Invalid arg count";
      for(int i=0;i<args.size()+1;i++)
      {
         switch(function.signature[i])
         {
            case sigInt: case sigFloat: case sigString: case sigObject:
               break; // Ok
            case sigVoid: 
               if (i==0) // return void ok
                  break;
               // fallthough
            default:
               throw "Bad haxe signature";
         }
      }
      switch(function.signature[0])
      {
         case sigInt: returnType = etInt; break;
         case sigFloat: returnType = etFloat; break;
         case sigString: returnType = etString; break;
         case sigObject: returnType = etObject; break;
         case sigVoid: returnType = etVoid; break;
         default: ;
      }

      if (thisExpr)
         thisExpr = thisExpr->link(inData);
      LinkExpressions(args,inData);

      return this;
   }

   const char *getName() { return "CallHaxe"; }

   template<typename T>
   void run(CppiaCtx *ctx,T &outValue)
   {
      unsigned char *pointer = ctx->pointer;
      ctx->pushObject(thisExpr ? thisExpr->runObject(ctx) : ctx->getThis());

      const char *s = function.signature+1;
      for(int a=0;a<args.size();a++)
      {
         CppiaExpr *arg = args[a];
         switch(*s++)
         {
            case sigInt: ctx->pushInt( arg->runInt(ctx) ); break;
            case sigFloat: ctx->pushFloat( arg->runFloat(ctx) ); break;
            case sigString: ctx->pushString( arg->runString(ctx) ); break;
            case sigObject: ctx->pushObject( arg->runObject(ctx) ); break;
            default: ;// huh?
         }
      }

      AutoStack a(ctx,pointer);
      function.execute(ctx);

      #ifdef DEBUG_RETURN_TYPE
      gLastRet = returnType;
      #endif
      if (sizeof(outValue)>0)
      {
         if (returnType == etInt)
            SetVal(outValue,ctx->getInt());
         else if (returnType == etFloat)
            SetVal(outValue,ctx->getFloat());
         else if (returnType == etString)
            SetVal(outValue,ctx->getString());
         else if (returnType == etObject)
            SetVal(outValue,ctx->getObject());
      }
   }

   void runVoid(CppiaCtx *ctx)
   {
      null val;
      run(ctx,val);
   }
   int runInt(CppiaCtx *ctx)
   {
      int val;
      run(ctx,val);
      return val;
   }
   Float runFloat(CppiaCtx *ctx)
   {
      Float val;
      run(ctx,val);
      return val;
   }
   String runString(CppiaCtx *ctx)
   {
      String val;
      run(ctx,val);
      return val;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      Dynamic val;
      run(ctx,val);
      return val.mPtr;
   }
};

struct CallStatic : public CppiaExpr
{
   int classId;
   int fieldId;
   Expressions args;
  
   CallStatic(CppiaStream &stream)
   {
      classId = stream.getInt();
      fieldId = stream.getInt();
      ReadExpressions(args,stream);
   }

   const char *getName() { return "CallStatic"; }
   CppiaExpr *link(CppiaData &inData)
   {

      TypeData *type = inData.types[classId];
      String field = inData.strings[fieldId];

      CppiaExpr *replace = 0;
      if (type->cppiaClass)
      {
         ScriptCallable *func = (ScriptCallable *)type->cppiaClass->findFunction(true,fieldId);
         if (!func)
         {
            printf("Could not find static function %s in %s\n", field.__s, type->name.__s);
         }
         else
         {
            replace = new CallFunExpr( this, 0, func, args );
         }
      }

      if (!replace && type->haxeClass.mPtr)
      {
         // TODO - might change if dynamic function (eg, trace)
         Dynamic func = type->haxeClass.mPtr->__Field( field, false );
         if (func.mPtr)
            replace = new CallDynamicFunction(inData, this, func, args );
      }

      // TODO - optimise...
      if (!replace && type->name==HX_CSTRING("String") && field==HX_CSTRING("fromCharCode"))
         replace = new CallDynamicFunction(inData, this, String::fromCharCode_dyn(), args );
         

      //printf(" static call to %s::%s (%d)\n", type->name.__s, field.__s, type->cppiaClass!=0);
      if (replace)
      {
         delete this;
         replace->link(inData);
         return replace;
      }

      printf("Unknown static call to %s::%s (%d)\n", type->name.__s, field.__s, type->cppiaClass!=0);
      inData.where(this);
      throw "Bad link";
      return this;
   }
};



struct CallGetIndex : public CppiaIntExpr
{
   CppiaExpr   *thisExpr;
  
   CallGetIndex(CppiaExpr *inSrc, CppiaExpr *inThis) : CppiaIntExpr(inSrc)
   {
      thisExpr = inThis;
   }

   const char *getName() { return "__Index"; }
   CppiaExpr *link(CppiaData &inData)
   {
      thisExpr = thisExpr->link(inData);
      return this;
   }
   int runInt(CppiaCtx *ctx)
   {
      hx::Object *obj = thisExpr->runObject(ctx);
      CPPIA_CHECK(obj);
      return obj->__Index();
   }
};


struct CallSetField : public CppiaDynamicExpr
{
   CppiaExpr   *thisExpr;
   CppiaExpr   *nameExpr;
   CppiaExpr   *valueExpr;
   CppiaExpr   *isPropExpr;
  
   CallSetField(CppiaExpr *inSrc, CppiaExpr *inThis, CppiaExpr *inName, CppiaExpr *inValue, CppiaExpr *inProp) :
      CppiaDynamicExpr(inSrc)
   {
      thisExpr = inThis;
      nameExpr = inName;
      valueExpr = inValue;
      isPropExpr = inProp;
   }

   const char *getName() { return "__SetField"; }
   CppiaExpr *link(CppiaData &inData)
   {
      thisExpr = thisExpr->link(inData);
      nameExpr = nameExpr->link(inData);
      valueExpr = valueExpr->link(inData);
      isPropExpr = isPropExpr->link(inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = thisExpr->runObject(ctx);
      CPPIA_CHECK(obj);
      String name = nameExpr->runString(ctx);
      hx::Object *value = valueExpr->runObject(ctx);
      bool isProp = isPropExpr->runInt(ctx);
      return obj->__SetField(name, Dynamic(value), isProp).mPtr;
   }
};


struct CallGetField : public CppiaDynamicExpr
{
   CppiaExpr   *thisExpr;
   CppiaExpr   *nameExpr;
   CppiaExpr   *isPropExpr;
  
   CallGetField(CppiaExpr *inSrc, CppiaExpr *inThis, CppiaExpr *inName, CppiaExpr *inProp) : CppiaDynamicExpr(inSrc)
   {
      thisExpr = inThis;
      nameExpr = inName;
      isPropExpr = inProp;
   }

   const char *getName() { return "__Field"; }
   CppiaExpr *link(CppiaData &inData)
   {
      thisExpr = thisExpr->link(inData);
      nameExpr = nameExpr->link(inData);
      isPropExpr = isPropExpr->link(inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = thisExpr->runObject(ctx);
      CPPIA_CHECK(obj);
      String name = nameExpr->runString(ctx);
      bool isProp = isPropExpr->runInt(ctx);
      return obj->__Field(name,isProp).mPtr;
   }
};





struct CallMemberVTable : public CppiaExpr
{
   Expressions args;
   CppiaExpr   *thisExpr;
   int         slot;
   ExprType    returnType;
   bool isInterfaceCall;

   CallMemberVTable(CppiaExpr *inSrc, CppiaExpr *inThis, int inVTableSlot, ExprType inReturnType,
             bool inIsInterfaceCall,Expressions &ioArgs)
      : CppiaExpr(inSrc), returnType(inReturnType)
   {
      args.swap(ioArgs);
      slot = inVTableSlot;
      thisExpr = inThis;
      isInterfaceCall = inIsInterfaceCall;
   }
   const char *getName() { return "CallMemberVTable"; }
   CppiaExpr *link(CppiaData &inData)
   {
      if (thisExpr)
         thisExpr = thisExpr->link(inData);
      LinkExpressions(args,inData);
      return this;
   }
   ExprType getType() { return returnType; }

   #define CALL_VTABLE_SETUP \
      hx::Object *thisVal = thisExpr ? thisExpr->runObject(ctx) : ctx->getThis(); \
      ScriptCallable **vtable = (ScriptCallable **)thisVal->__GetScriptVTable(); \
      if (isInterfaceCall) thisVal = thisVal->__GetRealObject(); \
      unsigned char *pointer = ctx->pointer; \
      vtable[slot]->pushArgs(ctx, thisVal, args); \
      AutoStack save(ctx,pointer);

   void runVoid(CppiaCtx *ctx)
   {
      CALL_VTABLE_SETUP
      ctx->runVoid(vtable[slot]);
   }
   int runInt(CppiaCtx *ctx)
   {
      CALL_VTABLE_SETUP
      return runContextConvertInt(ctx, returnType, vtable[slot]); 
   }
 
   Float runFloat(CppiaCtx *ctx)
   {
      CALL_VTABLE_SETUP
      return runContextConvertFloat(ctx, returnType, vtable[slot]); 
   }
   String runString(CppiaCtx *ctx)
   {
      CALL_VTABLE_SETUP
      return runContextConvertString(ctx, returnType, vtable[slot]); 
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      CALL_VTABLE_SETUP
      return runContextConvertObject(ctx, returnType, vtable[slot]); 
   }


};

/*
template<typename T>
struct CallArray : public CppiaExpr
{
   Expressions args;
   CppiaExpr   *thisExpr;
   std::string funcName;

   CallArray(const CppiaExpr *inSrc, CppiaExpr *inThisExpr, String inFunc, Expressions &ioArgs )
      : CppiaExpr(inSrc)
   {
      args.swap(ioArgs);
      thisExpr = inThisExpr;
      funcName = inFunc.__s;
   }

   const char *getName() { return "CallArray"; }
   CppiaExpr *link(CppiaData &inData)
   {
      LinkExpressions(args,inData);
      if (thisExpr)
         thisExpr = thisExpr->link(inData);
      return this;
   }

   void runVoid(CppiaCtx *ctx)
   {
      Array_obj<T> *ptr = (Array_obj<T> *) thisExpr->runObject(ctx);
      if (funcName=="push")
      {
         ptr->push( Dynamic(args[0]->runObject(ctx) ) );
      }
   }
};
*/

enum MemberCallType
{
   callObject,
   callThis,
   callSuperNew,
   callSuper,
};

struct CallMember : public CppiaExpr
{
   int classId;
   int fieldId;
   CppiaExpr *thisExpr;
   Expressions args;
   bool    callSuperField;
  
   CallMember(CppiaStream &stream,MemberCallType inCall)
   {
      classId = stream.getInt();
      fieldId = inCall==callSuperNew ? 0 : stream.getInt();
      //printf("fieldId = %d (%s)\n",fieldId,stream.cppiaData->strings[ fieldId ].__s);
      int n = stream.getInt();
      thisExpr = inCall==callObject ? createCppiaExpr(stream) : 0;
      callSuperField = inCall==callSuper;
      ReadExpressions(args,stream,n);
   }

   CppiaExpr *linkSuperCall(CppiaData &inData)
   {
      TypeData *type = inData.types[classId];

      // TODO - callSuperNew

      if (type->cppiaClass)
      {
         //printf("Using cppia super %p %p\n", type->cppiaClass->newFunc, type->cppiaClass->newFunc->funExpr);
         CppiaExpr *replace = new CallFunExpr( this, 0, (ScriptCallable*)type->cppiaClass->newFunc->funExpr, args );
         replace->link(inData);
         delete this;
         return replace;
      }
      //printf("Using haxe super\n");
      ScriptRegistered *superReg = (*sScriptRegistered)[type->name.__s];
      if (!superReg)
      {
         printf("No class registered for %s\n", type->name.__s);
         throw "Unknown super call";
      }
      if (!superReg->construct.execute)
      {
         //printf("Call super - nothing to do...\n");
         CppiaExpr *replace = new CppiaExpr(this);
         delete this;
         return replace;
      }
      CppiaExpr *replace = new CallHaxe( this, superReg->construct,0, args );
      replace->link(inData);
      delete this;
      return replace;
   }



   const char *getName() { return "CallMember"; }
   CppiaExpr *link(CppiaData &inData)
   {
      if (fieldId==0)
         return linkSuperCall(inData);

      TypeData *type = inData.types[classId];
      String field = inData.strings[fieldId];

      //printf("  linking call %s::%s\n", type->name.__s, field.__s);

      CppiaExpr *replace = 0;
      
      if (type->arrayType)
      {
         replace = createArrayBuiltin(this, type->arrayType, thisExpr, field, args);
         if (!replace)
         {
            if (field!=HX_CSTRING("__SetField") && field!=HX_CSTRING("__Field") && field!=HX_CSTRING("__Index"))
            {
               printf("Bad array field '%s'\n", field.__s);
               inData.where(this);
               throw "Unknown array field";
            }
         }
      }


      if (!replace && type->cppiaClass)
      {
         if (callSuperField)
         {
            // Bind now ...
            ScriptCallable *func = (ScriptCallable *)type->cppiaClass->findFunction(false, fieldId) ;
            if (func)
               replace = new CallFunExpr( this, thisExpr, func, args );
         }
         else
         {
            int vtableSlot = type->cppiaClass->findFunctionSlot(fieldId);
            //printf("   vslot %d\n", vtableSlot);
            if (vtableSlot>=0)
            {
               ExprType returnType = type->cppiaClass->findFunctionType(inData,fieldId);
               replace = new CallMemberVTable( this, thisExpr, vtableSlot, returnType, type->cppiaClass->isInterface, args );
            }
         }
      }
      if (!replace && type->haxeBase)
      {
         ScriptFunction func = type->haxeBase->findFunction(field.__s);
         if (func.signature)
         {
            //printf(" found function %s\n", func.signature );
            replace = new CallHaxe( this, func, thisExpr, args );
         }
      }

      if (!replace && type->interfaceBase)
      {
         ScriptFunction func = type->interfaceBase->findFunction(field.__s);
         if (func.signature)
         {
            //printf(" found function %s\n", func.signature );
            replace = new CallHaxe( this, func, thisExpr, args );
         }
      }

      if (!replace && type->name==HX_CSTRING("String"))
      {
         replace = createStringBuiltin(this, thisExpr, field, args);
      }

      if (!replace && field==HX_CSTRING("__Index"))
         replace = new CallGetIndex(this, thisExpr);
      if (!replace && field==HX_CSTRING("__SetField") && args.size()==3)
         replace = new CallSetField(this, thisExpr, args[0], args[1], args[2]);
      if (!replace && field==HX_CSTRING("__Field") && args.size()==2)
         replace = new CallGetField(this, thisExpr, args[0], args[1]);


      if (replace)
      {
         replace->link(inData);
         delete this;
         return replace;
      }

      printf("Could not link %s::%s\n", type->name.c_str(), field.__s );
      printf("%p %p\n", type->cppiaClass, type->haxeBase);
      if (type->cppiaClass)
         type->cppiaClass->dump();
      inData.where(this);
      throw "Bad linkage";

      return this;
   }
};



struct ThisExpr : public CppiaDynamicExpr
{
   ThisExpr(CppiaStream &stream)
   {
   }
   hx::Object *runObject(CppiaCtx *ctx) { return ctx->getThis(); }
};


struct ClassOfExpr : public CppiaExprWithValue
{
   int typeId;

   ClassOfExpr(CppiaStream &stream)
   {
      typeId = stream.getInt();
   }
   const char *getName() { return "ClassOfExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      TypeData *type = inData.types[typeId];
      if (type->cppiaClass)
         value.mPtr = type->cppiaClass->mClass.mPtr;
      else
         value.mPtr = type->haxeClass.mPtr;
      return CppiaExprWithValue::link(inData);
   }
};


struct CallGlobal : public CppiaExpr
{
   int fieldId;
   Expressions args;
  
   CallGlobal(CppiaStream &stream)
   {
      fieldId = stream.getInt();
      int n = stream.getInt();
      ReadExpressions(args,stream,n);
   }

   const char *getName() { return "CallGlobal"; }
   CppiaExpr *link(CppiaData &inData)
   {
      String name = inData.strings[fieldId];
      LinkExpressions(args,inData);
      return createGlobalBuiltin(this, name, args );
   }
};



struct Call : public CppiaDynamicExpr
{
   Expressions args;
   CppiaExpr   *func;
  
   Call(CppiaStream &stream)
   {
      int argCount = stream.getInt();
      func = createCppiaExpr(stream);
      ReadExpressions(args,stream,argCount);
   }
   const char *getName() { return "Call"; }
   CppiaExpr *link(CppiaData &inData)
   {
      func = func->link(inData);
      LinkExpressions(args,inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *funcVal = func->runObject(ctx);
      CPPIA_CHECK_FUNC(funcVal);
      int size = args.size();
      switch(size)
      {
         case 0:
           return funcVal->__run().mPtr;

         case 1:
           return funcVal->__run(args[0]->runObject(ctx)).mPtr;

         case 2:
           {
           Dynamic a0 = args[0]->runObject(ctx);
           Dynamic a1 = args[1]->runObject(ctx);
           return funcVal->__run(a0,a1).mPtr;
           }
         case 3:
           {
           Dynamic a0 = args[0]->runObject(ctx);
           Dynamic a1 = args[1]->runObject(ctx);
           Dynamic a2 = args[2]->runObject(ctx);
           return funcVal->__run(a0,a1,a2).mPtr;
           }
         case 4:
           {
           Dynamic a0 = args[0]->runObject(ctx);
           Dynamic a1 = args[1]->runObject(ctx);
           Dynamic a2 = args[2]->runObject(ctx);
           Dynamic a3 = args[3]->runObject(ctx);
           return funcVal->__run(a0,a1,a2,a3).mPtr;
           }
         case 5:
           {
           Dynamic a0 = args[0]->runObject(ctx);
           Dynamic a1 = args[1]->runObject(ctx);
           Dynamic a2 = args[2]->runObject(ctx);
           Dynamic a3 = args[3]->runObject(ctx);
           Dynamic a4 = args[4]->runObject(ctx);
           return funcVal->__run(a0,a1,a2,a3,a4).mPtr;
           }


         default:
            Array<Dynamic> argArray = Array_obj<Dynamic>::__new(size,size);
            for(int s=0;s<size;s++)
               argArray[s] = args[s]->runObject(ctx);

            return funcVal->__Run(argArray).mPtr;
      }
      return 0;
   }
};

struct FieldByName : public CppiaDynamicExpr
{
   CppiaExpr   *object;
   String      name;
   CppiaExpr   *value;
   AssignOp    assign;
   CrementOp   crement;
   Class       staticClass;

   
   FieldByName(CppiaExpr *inSrc, CppiaExpr *inObject, Class inStaticClass,
               String inName, AssignOp inAssign, CrementOp inCrement, CppiaExpr *inValue)
      : CppiaDynamicExpr(inSrc)
   {
      object = inObject;
      staticClass = inStaticClass;
      name = inName;
      assign = inAssign;
      crement = inCrement;
      value = inValue;

   }

   CppiaExpr *link(CppiaData &inData)
   {
      if (value)
         value = value->link(inData);
      if (object)
         object = object->link(inData);
      inData.markable.push_back(this);
      return this;
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = object ? object->runObject(ctx) : staticClass.mPtr ? staticClass.mPtr : ctx->getThis();
      CPPIA_CHECK(obj);

      if (crement==coNone && assign==aoNone)
         return obj->__Field(name,true).mPtr;

      if (crement!=coNone)
      {
         Dynamic val0 = obj->__Field(name,true);
         Dynamic val1 = val0 + (crement<=coPostInc ? 1 : -1);
         obj->__SetField(name, val1,true);
         return crement & coPostInc ? val0.mPtr : val1.mPtr;
      }
      if (assign == aoSet)
         return obj->__SetField(name, value->runObject(ctx),true).mPtr;

      Dynamic val0 = obj->__Field(name,true);
      Dynamic val1;

      switch(assign)
      {
         case aoAdd: val1 = val0 + Dynamic(value->runObject(ctx)); break;
         case aoMult: val1 = val0 * value->runFloat(ctx); break;
         case aoDiv: val1 = val0 / value->runFloat(ctx); break;
         case aoSub: val1 = val0 - value->runFloat(ctx); break;
         case aoAnd: val1 = (int)val0 & value->runInt(ctx); break;
         case aoOr: val1 = (int)val0 | value->runInt(ctx); break;
         case aoXOr: val1 = (int)val0 ^ value->runInt(ctx); break;
         case aoShl: val1 = (int)val0 << value->runInt(ctx); break;
         case aoShr: val1 = (int)val0 >> value->runInt(ctx); break;
         case aoUShr: val1 = hx::UShr((int)val0,value->runInt(ctx)); break;
         case aoMod: val1 = hx::Mod(val0 ,value->runFloat(ctx)); break;
         default: ;
      }
      obj->__SetField(name,val1,true);
      return val1.mPtr;
   }

   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(name);
      HX_MARK_MEMBER(staticClass);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(name);
      HX_VISIT_MEMBER(staticClass);
   }


};

struct GetFieldByName : public CppiaDynamicExpr
{
   int         nameId;
   int         classId;
   int         vtableSlot;
   CppiaExpr   *object;
   String      name;
   bool        isInterface;
   bool        isStatic;
   Class       staticClass;
  
   GetFieldByName(CppiaStream &stream,bool isThisObject,bool inIsStatic=false)
   {
      classId = stream.getInt();
      nameId = stream.getInt();
      isStatic = inIsStatic;
      object = (isThisObject||isStatic) ? 0 : createCppiaExpr(stream);
      name.__s = 0;
      isInterface = false;
      vtableSlot = -1;
   }
   GetFieldByName(const CppiaExpr *inSrc, int inNameId, CppiaExpr *inObject)
      : CppiaDynamicExpr(inSrc)
   {
      classId = 0;
      nameId = inNameId;
      object = inObject;
      isInterface = false;
      name.__s = 0;
      vtableSlot = -1;
   }
   const char *getName() { return "GetFieldByName"; }

   CppiaExpr *link(CppiaData &inData)
   {
      if (object)
         object = object->link(inData);
      TypeData *type = inData.types[classId];
      if (!isStatic && type->cppiaClass)
      {
         vtableSlot  = type->cppiaClass->findFunctionSlot(nameId);
         isInterface = type->cppiaClass->isInterface;
      }
      else if (isStatic)
      {
         if (type->cppiaClass)
         {
            CppiaVar *var = type->cppiaClass->findVar(true,nameId);
            if (var)
            {
               CppiaExpr *replace = var->createAccess(this);
               if (!replace)
                  throw "Bad replace";
               replace->link(inData);
               delete this;
               return replace;
            }

            CppiaExpr *func = type->cppiaClass->findFunction(true,nameId);
            if (func)
            {
               CppiaExprWithValue *replace = new CppiaExprWithValue(this);
               replace->link(inData);
               replace->value = createMemberClosure(0, ((ScriptCallable *)func));
               delete this;
               return replace;
            }
         }

         staticClass = type->haxeClass;
         if (!staticClass.mPtr)
         {
            inData.where(this);
            if (type->cppiaClass)
               type->cppiaClass->dump();
            printf("Could not link static %s::%s (%d)\n", type->name.c_str(), inData.strings[nameId].__s, nameId );
            throw "Bad link";
         }
      }

      // Use runtime lookup...
      if (vtableSlot<0)
      {
         name = inData.strings[nameId];
         inData.markable.push_back(this);
      }
      return this;
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *instance = object ? object->runObject(ctx) : isStatic ? staticClass.mPtr : ctx->getThis();
      CPPIA_CHECK(instance);
      if (vtableSlot>=0)
      {
         if (isInterface)
            instance = instance->__GetRealObject();
 
         ScriptCallable **vtable = (ScriptCallable **)instance->__GetScriptVTable();
         ScriptCallable *func = vtable[vtableSlot];
         return new (sizeof(hx::Object *)) CppiaClosure(instance, func);
      }
      return instance->__Field(name,true).mPtr;
   }
  
   CppiaExpr   *makeSetter(AssignOp inOp,CppiaExpr *inValue)
   {
      // delete this - remove markable?
      return new FieldByName(this, object, staticClass, name, inOp, coNone, inValue);
   }

   CppiaExpr   *makeCrement(CrementOp inOp)
   {
      // delete this - remove markable?
      return new FieldByName(this, object, staticClass, name, aoNone, inOp, 0);
   }

   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(name);
      HX_MARK_MEMBER(staticClass);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(name);
      HX_VISIT_MEMBER(staticClass);
   }

};



template<typename T, int REFMODE> 
struct MemReference : public CppiaExpr
{
   int  offset;
   T *pointer;
   CppiaExpr *object;

   #define MEMGETVAL \
     *(T *)( \
         ( REFMODE==locObj      ?(char *)object->runObject(ctx) : \
           REFMODE==locAbsolute ?(char *)pointer : \
           REFMODE==locThis     ?(char *)ctx->getThis() : \
                                 (char *)ctx->frame \
         ) + offset )

   MemReference(const CppiaExpr *inSrc, int inOffset, CppiaExpr *inExpr=0)
      : CppiaExpr(inSrc)
   {
      object = inExpr;
      offset = inOffset;
      pointer = 0;
   }
   MemReference(CppiaExpr *inSrc, T *inPointer)
      : CppiaExpr(inSrc)
   {
      object = 0;
      offset = 0;
      pointer = inPointer;
   }
 
   ExprType getType()
   {
      return (ExprType) ExprTypeOf<T>::value;
   }
   const char *getName() { return "MemReference"; }
   CppiaExpr *link(CppiaData &inData)
   {
      if (object)
         object = object->link(inData);
      if (REFMODE==locAbsolute) // Only for string/object?
         inData.markable.push_back(this);
 
      return this;
   }

   void mark(hx::MarkContext *__inCtx) { HX_MARK_MEMBER( *pointer ); }
   void visit(hx::VisitContext *__inCtx) { HX_VISIT_MEMBER( *pointer ); }


   void        runVoid(CppiaCtx *ctx) { }
   int runInt(CppiaCtx *ctx) { return ValToInt( MEMGETVAL ); }
   Float       runFloat(CppiaCtx *ctx)
   {
      return ValToFloat( MEMGETVAL );
   }
   ::String    runString(CppiaCtx *ctx) { return ValToString( MEMGETVAL ); }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      return Dynamic( MEMGETVAL ).mPtr;
   }

   CppiaExpr  *makeSetter(AssignOp op,CppiaExpr *value);
   CppiaExpr  *makeCrement(CrementOp inOp);
};


// TODO - strore type
CppiaExpr *createStaticAccess(CppiaExpr *inSrc,ExprType inType, void *inPtr)
{
   switch(inType)
   {
      case etInt : return new MemReference<int,locAbsolute>(inSrc, (int *)inPtr );
      case etFloat : return new MemReference<Float,locAbsolute>(inSrc, (Float *)inPtr );
      case etString : return new MemReference<String,locAbsolute>(inSrc, (String *)inPtr );
      case etObject : return new MemReference<hx::Object *,locAbsolute>(inSrc, (hx::Object **)inPtr );
      default:
         return 0;
   }
}



template<typename T, int REFMODE, typename Assign> 
struct MemReferenceSetter : public CppiaExpr
{
   int offset;
   T         *pointer;
   CppiaExpr *object;
   CppiaExpr *value;

   MemReferenceSetter(MemReference<T,REFMODE> *inSrc, CppiaExpr *inValue) : CppiaExpr(inSrc)
   {
      offset = inSrc->offset;
      object = inSrc->object;
      pointer = inSrc->pointer;
      value = inValue;
   }
   ExprType getType()
   {
      return (ExprType) ExprTypeOf<T>::value;
   }

   void runVoid(CppiaCtx *ctx)
   {
      Assign::run( MEMGETVAL, ctx, value);
   }
   int runInt(CppiaCtx *ctx)
   {
       return ValToInt( Assign::run(MEMGETVAL,ctx, value ) );
   }
   Float runFloat(CppiaCtx *ctx)
   {
      return ValToFloat( Assign::run(MEMGETVAL,ctx, value) );
   }
   ::String runString(CppiaCtx *ctx)
   {
      return ValToString( Assign::run( MEMGETVAL,ctx, value) );
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      return Dynamic( Assign::run(MEMGETVAL,ctx,value) ).mPtr;
   }

   void mark(hx::MarkContext *__inCtx) { HX_MARK_MEMBER( *pointer ); }
   void visit(hx::VisitContext *__inCtx) { HX_VISIT_MEMBER( *pointer ); }

   CppiaExpr *link(CppiaData &inData)
   {
      if (REFMODE==locAbsolute) // Only for string/object?
         inData.markable.push_back(this);
      return this;
   }


};


template<typename T, int REFMODE> 
CppiaExpr *MemReference<T,REFMODE>::makeSetter(AssignOp op,CppiaExpr *value)
{
   switch(op)
   {
      case aoSet:
         return new MemReferenceSetter<T,REFMODE,AssignSet>(this,value);
      case aoAdd:
         return new MemReferenceSetter<T,REFMODE,AssignAdd>(this,value);
      case aoMult:
         return new MemReferenceSetter<T,REFMODE,AssignMult>(this,value);
      case aoDiv:
         return new MemReferenceSetter<T,REFMODE,AssignDiv>(this,value);
      case aoSub:
         return new MemReferenceSetter<T,REFMODE,AssignSub>(this,value);
      case aoAnd:
         return new MemReferenceSetter<T,REFMODE,AssignAnd>(this,value);
      case aoOr:
         return new MemReferenceSetter<T,REFMODE,AssignOr>(this,value);
      case aoXOr:
         return new MemReferenceSetter<T,REFMODE,AssignXOr>(this,value);
      case aoShl:
         return new MemReferenceSetter<T,REFMODE,AssignShl>(this,value);
      case aoShr:
         return new MemReferenceSetter<T,REFMODE,AssignShr>(this,value);
      case aoUShr:
         return new MemReferenceSetter<T,REFMODE,AssignUShr>(this,value);
      case aoMod:
         return new MemReferenceSetter<T,REFMODE,AssignMod>(this,value);
      default: ;
   }
   throw "Bad assign op";
   return 0;
}



template<typename T, int REFMODE,typename CREMENT> 
struct MemReferenceCrement : public CppiaExpr
{
   int offset;
   T   *pointer;
   CppiaExpr *object;

   MemReferenceCrement(MemReference<T,REFMODE> *inSrc) : CppiaExpr(inSrc)
   {
      offset = inSrc->offset;
      object = inSrc->object;
      pointer = inSrc->pointer;
   }
   ExprType getType()
   {
      return (ExprType) ExprTypeOf<T>::value;
   }

   void        runVoid(CppiaCtx *ctx) { CREMENT::run( MEMGETVAL ); }
   int runInt(CppiaCtx *ctx) { return ValToInt( CREMENT::run(MEMGETVAL) ); }
   Float       runFloat(CppiaCtx *ctx) { return ValToFloat( CREMENT::run(MEMGETVAL)); }
   ::String    runString(CppiaCtx *ctx) { return ValToString( CREMENT::run( MEMGETVAL) ); }

   hx::Object *runObject(CppiaCtx *ctx) { return Dynamic( CREMENT::run(MEMGETVAL) ).mPtr; }


   void mark(hx::MarkContext *__inCtx) { HX_MARK_MEMBER( *pointer ); }
   void visit(hx::VisitContext *__inCtx) { HX_VISIT_MEMBER( *pointer ); }

   CppiaExpr *link(CppiaData &inData)
   {
      if (REFMODE==locAbsolute) // Only for string/object?
         inData.markable.push_back(this);
      return this;
   }


};



template<typename T, int REFMODE> 
CppiaExpr *MemReference<T,REFMODE>::makeCrement(CrementOp inOp)
{
   switch(inOp)
   {
      case coPreInc:
         return new MemReferenceCrement<T,REFMODE,CrementPreInc>(this);
      case coPostInc:
         return new MemReferenceCrement<T,REFMODE,CrementPostInc>(this);
      case coPreDec:
         return new MemReferenceCrement<T,REFMODE,CrementPreDec>(this);
      case coPostDec:
         return new MemReferenceCrement<T,REFMODE,CrementPostDec>(this);
      default:
         break;
   }
   throw "bad crement op";
   return 0;
}

struct GetFieldByLinkage : public CppiaExpr
{
   int         fieldId;
   int         typeId;
   CppiaExpr   *object;
  
   GetFieldByLinkage(CppiaStream &stream,bool inThisObject)
   {
      typeId = stream.getInt();
      fieldId = stream.getInt();
      object = inThisObject ? 0 : createCppiaExpr(stream);
   }
   const char *getName() { return "GetFieldByLinkage"; }
   CppiaExpr *link(CppiaData &inData)
   {
      TypeData *type = inData.types[typeId];
      String field = inData.strings[fieldId];

      int offset = 0;
      CppiaExpr *replace = 0;
      FieldStorage storeType = fsUnknown;

      if (type->haxeClass.mPtr)
      {
         const StorageInfo *store = type->haxeClass.mPtr->GetMemberStorage(field);
         if (store)
         {
            offset = store->offset;
            storeType = store->type;
            DBGLOG(" found haxe var %s::%s = %d (%d)\n", type->haxeClass->mName.__s, field.__s, offset, storeType);
         }
      }

      if (!offset && type->cppiaClass)
      {
         CppiaVar *var = type->cppiaClass->findVar(false, fieldId);
         if (var)
         {
            offset = var->offset;
            storeType = var->storeType;
            DBGLOG(" found script var %s = %d (%d)\n", field.__s, offset, storeType);
         }
      }

      if (offset)
      {
         switch(storeType)
         {
            case fsBool:
               replace = object ?
                     (CppiaExpr*)new MemReference<bool,locObj>(this,offset,object):
                     (CppiaExpr*)new MemReference<bool,locThis>(this,offset);
                  break;
            case fsInt:
               replace = object ?
                     (CppiaExpr*)new MemReference<int,locObj>(this,offset,object):
                     (CppiaExpr*)new MemReference<int,locThis>(this,offset);
                  break;
            case fsFloat:
               replace = object ?
                     (CppiaExpr*)new MemReference<Float,locObj>(this,offset,object):
                     (CppiaExpr*)new MemReference<Float,locThis>(this,offset);
                  break;
            case fsString:
               replace = object ?
                     (CppiaExpr*)new MemReference<String,locObj>(this,offset,object):
                     (CppiaExpr*)new MemReference<String,locThis>(this,offset);
                  break;
            case fsObject:
               replace = object ?
                     (CppiaExpr*)new MemReference<hx::Object *,locObj>(this,offset,object):
                     (CppiaExpr*)new MemReference<hx::Object *,locThis>(this,offset);
                  break;
            case fsByte:
            case fsUnknown:
                ;// todo
         }
      }

      if (!replace && type->arrayType!=arrNotArray && field==HX_CSTRING("length"))
      {
         int offset = (int) offsetof( Array_obj<int>, length );
         replace = object ?
             (CppiaExpr*)new MemReference<int,locObj>(this,offset,object):
             (CppiaExpr*)new MemReference<int,locThis>(this,offset);
      }

      if (!replace && type->name==HX_CSTRING("String") && field==HX_CSTRING("length"))
      {
         int offset = (int) offsetof( Array_obj<int>, length );
         replace = object ?
             (CppiaExpr*)new MemReference<int,locObj>(this,offset,object):
             (CppiaExpr*)new MemReference<int,locThis>(this,offset);
      }



      if (replace)
      {
         replace = replace->link(inData);
         delete this;
         return replace;
      }

      // It is ok for interfaces to look up members by name
      if (!type->isInterface)
      {
         printf("   GetFieldByLinkage %s (%p %p) '%s' fallback\n", type->name.__s, type->haxeClass.mPtr, type->cppiaClass, field.__s);
         if (type->cppiaClass)
            type->cppiaClass->dump();
      }

      CppiaExpr *result = new GetFieldByName(this, fieldId, object);
      result = result->link(inData);
      delete this;
      return result;
   }
};




struct StringVal : public CppiaExprWithValue
{
   int stringId;
   String strVal;

   StringVal(int inId) : stringId(inId)
   {
   }

   ExprType getType() { return etString; }

   const char *getName() { return "StringVal"; }
   CppiaExpr *link(CppiaData &inData)
   {
      strVal = inData.strings[stringId];
      //printf("Linked %d -> %s\n", stringId, strVal.__s);
      return CppiaExprWithValue::link(inData);
   }
   ::String    runString(CppiaCtx *ctx)
   {
      return strVal;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      if (!value.mPtr)
         value = strVal;
      return value.mPtr;
   }

   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(value);
      HX_MARK_MEMBER(strVal);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(value);
      HX_VISIT_MEMBER(strVal);
   }

};


template<typename T>
struct DataVal : public CppiaExprWithValue
{
   T data;

   DataVal(T inVal) : data(inVal)
   {
   }

   ExprType getType() { return (ExprType)ExprTypeOf<T>::value; }

   void        runVoid(CppiaCtx *ctx) {  }
   int runInt(CppiaCtx *ctx) { return ValToInt(data); }
   Float       runFloat(CppiaCtx *ctx) { return ValToFloat(data); }
   ::String    runString(CppiaCtx *ctx) { return ValToString(data); }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      if (!value.mPtr)
         value = Dynamic(data);
      return value.mPtr;
   }
};



struct NullVal : public CppiaExpr
{
   NullVal() { }
   ExprType getType() { return etObject; }

   void        runVoid(CppiaCtx *ctx) {  }
   int runInt(CppiaCtx *ctx) { return 0; }
   Float       runFloat(CppiaCtx *ctx) { return 0.0; }
   ::String    runString(CppiaCtx *ctx) { return null(); }
   hx::Object  *runObject(CppiaCtx *ctx) { return 0; }

};


struct PosInfo : public CppiaExprWithValue
{
   int fileId;
   int line;
   int classId;
   int methodId;

   PosInfo(CppiaStream &stream)
   {
      fileId = stream.getInt();
      line = stream.getInt();
      classId = stream.getInt();
      methodId = stream.getInt();
   }
   const char *getName() { return "PosInfo"; }
   CppiaExpr *link(CppiaData &inData)
   {
      String clazz = inData.strings[classId];
      String file = inData.strings[fileId];
      String method = inData.strings[methodId];
      value = hx::SourceInfo(file,line,clazz,method);
      return CppiaExprWithValue::link(inData);
   }
};


struct ObjectDef : public CppiaDynamicExpr
{
   int fieldCount;
   std::vector<int> stringIds;
   CppiaData *data;
   ArrayType arrayType;
   Expressions values;

   ObjectDef(CppiaStream &stream)
   {
      fieldCount = stream.getInt();
      data = 0;
      stringIds.resize(fieldCount);
      for(int i=0;i<fieldCount;i++)
         stringIds[i] = stream.getInt();
      ReadExpressions(values,stream,fieldCount);
   }

   const char *getName() { return "ObjectDef"; }
   CppiaExpr *link(CppiaData &inData)
   {
      data = &inData;
      LinkExpressions(values,inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Anon result = hx::Anon_obj::Create();
      for(int i=0;i<fieldCount;i++)
         result->Add(data->strings[stringIds[i]], values[i]->runObject(ctx), false );
      return result.mPtr;
   }
};

struct ArrayDef : public CppiaDynamicExpr
{
   int classId;
   Expressions items;
   ArrayType arrayType;

   ArrayDef(CppiaStream &stream)
   {
      classId = stream.getInt();
      ReadExpressions(items,stream);
   }


   const char *getName() { return "ArrayDef"; }
   CppiaExpr *link(CppiaData &inData)
   {
      TypeData *type = inData.types[classId];
      arrayType = type->arrayType;
      if (!arrayType)
      {
         printf("ArrayDef of non array-type %s\n", type->name.__s);
         throw "Bad ArrayDef";
      }
      LinkExpressions(items,inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      int n = items.size();
      switch(arrayType)
      {
         case arrBool:
            { 
            Array<bool> result = Array_obj<bool>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = items[i]->runInt(ctx)!=0;
            return result.mPtr;
            }
         case arrUnsignedChar:
            { 
            Array<unsigned char> result = Array_obj<unsigned char>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = items[i]->runInt(ctx);
            return result.mPtr;
            }
         case arrInt:
            { 
            Array<int> result = Array_obj<int>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = items[i]->runInt(ctx);
            return result.mPtr;
            }
         case arrFloat:
            { 
            Array<Float> result = Array_obj<Float>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = items[i]->runFloat(ctx);
            return result.mPtr;
            }
         case arrString:
            { 
            Array<String> result = Array_obj<String>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = items[i]->runString(ctx);
            return result.mPtr;
            }
         case arrObject:
         case arrAny:
            { 
            Array<Dynamic> result = Array_obj<Dynamic>::__new(n,n);
            for(int i=0;i<n;i++)
               result[i] = items[i]->runObject(ctx);
            return result.mPtr;
            }
         default:
            return 0;
      }
      return 0;
   }
};

struct DynamicArrayI : public CppiaDynamicExpr
{
   CppiaExpr *object;
   CppiaExpr *index;
   CppiaExpr *value;
   AssignOp  assign;
   CrementOp crement;

   DynamicArrayI(CppiaExpr *inSrc, CppiaExpr *inObj, CppiaExpr *inI)
     : CppiaDynamicExpr(inSrc)
   {
      object = inObj;
      index = inI;
      value = 0;
      assign = aoNone;
      crement = coNone;
   }
   CppiaExpr *link(CppiaData &inData)
   {
      object = object->link(inData);
      index = index->link(inData);
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = object->runObject(ctx);
      CPPIA_CHECK(obj);
      int i = index->runInt(ctx);
      if (crement==coNone && assign==aoNone)
      {
         return obj->__GetItem(i).mPtr;
      }

      if (crement!=coNone)
      {
         Dynamic val0 = obj->__GetItem(i);
         Dynamic val1 = val0 + (crement<=coPostInc ? 1 : -1);
         obj->__SetItem(i, val1);
         return crement & coPostInc ? val0.mPtr : val1.mPtr;
      }
      if (assign == aoSet)
         return obj->__SetItem(i, value->runObject(ctx)).mPtr;

      Dynamic val0 = obj->__GetItem(i);
      Dynamic val1;

      switch(assign)
      {
         case aoAdd: val1 = val0 + Dynamic(value->runObject(ctx)); break;
         case aoMult: val1 = val0 * value->runFloat(ctx); break;
         case aoDiv: val1 = val0 / value->runFloat(ctx); break;
         case aoSub: val1 = val0 - value->runFloat(ctx); break;
         case aoAnd: val1 = (int)val0 & value->runInt(ctx); break;
         case aoOr: val1 = (int)val0 | value->runInt(ctx); break;
         case aoXOr: val1 = (int)val0 ^ value->runInt(ctx); break;
         case aoShl: val1 = (int)val0 << value->runInt(ctx); break;
         case aoShr: val1 = (int)val0 >> value->runInt(ctx); break;
         case aoUShr: val1 = hx::UShr((int)val0,value->runInt(ctx)); break;
         case aoMod: val1 = hx::Mod(val0 ,value->runFloat(ctx)); break;
         default: ;
      }
      obj->__SetItem(i,val1);
      return val1.mPtr;
   }
   CppiaExpr  *makeSetter(AssignOp op,CppiaExpr *inValue)
   {
      assign = op;
      value = inValue;
      return this;
   }
   CppiaExpr  *makeCrement(CrementOp inOp)
   {
      crement = inOp;
      return this;
   }
};

struct ArrayIExpr : public CppiaExpr
{
   int       classId;
   CppiaExpr *thisExpr;
   CppiaExpr *iExpr;
   CppiaExpr *value;
   CrementOp crementOp;
   AssignOp  assignOp;


   ArrayIExpr(CppiaStream &stream)
   {
      classId = stream.getInt();
      thisExpr = createCppiaExpr(stream);
      iExpr = createCppiaExpr(stream);
      value = 0;
      crementOp = coNone;
      assignOp = aoNone;
   }

   const char *getName() { return "ArrayIExpr"; }

   CppiaExpr *link(CppiaData &inData)
   {
      TypeData *type = inData.types[classId];

      CppiaExpr *replace = 0;

      if (type->name==HX_CSTRING("Dynamic"))
      {
         replace = new DynamicArrayI(this, thisExpr, iExpr);
      }
      else
      {
         if (!type->arrayType)
         {
            printf("ARRAYI of non array-type %s\n", type->name.__s);
            throw "Bad ARRAYI";
         }
         Expressions val;
         val.push_back(iExpr);
   
         replace = createArrayBuiltin(this, type->arrayType, thisExpr, HX_CSTRING("__get"), val);
      }
      replace = replace->link(inData);

      delete this;
      return replace;
   }

};


struct EnumIExpr : public CppiaDynamicExpr
{
   CppiaExpr *object;
   int       index;
   int       classId;


   EnumIExpr(CppiaStream &stream)
   {
      classId = stream.getInt();
      index = stream.getInt();
      object = createCppiaExpr(stream);
   }

   const char *getName() { return "EnumIExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      object = object->link(inData);
      return this;
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      hx::Object *obj = object->runObject(ctx);
      return obj->__EnumParams()[index].mPtr;
   }

};



struct VarDecl : public CppiaVoidExpr
{
   CppiaStackVar var;
   CppiaExpr *init;

   VarDecl(CppiaStream &stream,bool inInit)
   {
      var.fromStream(stream);
      init = 0;
      if (inInit)
      {
         int t = stream.getInt();
         init = createCppiaExpr(stream);
         init->haxeTypeId = t;
      }
   }

   void runVoid(CppiaCtx *ctx)
   {
      if (init)
      {
         // TODO  - store type
         switch(var.expressionType)
         {
            case etInt: *(int *)(ctx->frame+var.stackPos) = init->runInt(ctx); break;
            case etFloat: *(Float *)(ctx->frame+var.stackPos) = init->runFloat(ctx); break;
            case etString: *(String *)(ctx->frame+var.stackPos) = init->runString(ctx); break;
            case etObject: *(hx::Object **)(ctx->frame+var.stackPos) = init->runObject(ctx); break;
            case etVoid:
            case etNull:
               break;
          }
      }
   }

   const char *getName() { return "VarDecl"; }
   CppiaExpr *link(CppiaData &inData)
   {
      var.link(inData);
      init = init ? init->link(inData) : 0;
      return this;
   }
};

struct TVars : public CppiaVoidExpr
{
   Expressions vars;

   TVars(CppiaStream &stream)
   {
      int count = stream.getInt();
      vars.resize(count);
      for(int i=0;i<count;i++)
      {
         std::string tok = stream.getToken();
         if (tok=="VARDECL")
            vars[i] = new VarDecl(stream,false);
         else if (tok=="VARDECLI")
            vars[i] = new VarDecl(stream,true);
         else
            throw "Bad var decl";
      }
   }
   CppiaExpr *link(CppiaData &inData)
   {
      LinkExpressions(vars,inData);
      return this;
   }
   
   void runVoid(CppiaCtx *ctx)
   {
      for(int i=0;i<vars.size();i++)
         vars[i]->runVoid(ctx);
   }
};

struct ForExpr : public CppiaVoidExpr
{
   CppiaStackVar var;
   CppiaExpr *init;
   CppiaExpr *loop;

 
   ForExpr(CppiaStream &stream)
   {
      var.fromStream(stream);
      init = createCppiaExpr(stream);
      loop = createCppiaExpr(stream);
   }

   const char *getName() { return "ForExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      var.link(inData);
      init = init->link(inData);
      loop = loop->link(inData);
      return this;
   }

   void runVoid(CppiaCtx *ctx)
   {
      hx::Object *iterator = init->runObject(ctx);
      CPPIA_CHECK(iterator);
      Dynamic  hasNext = iterator->__Field(HX_CSTRING("hasNext"),true);
      CPPIA_CHECK(hasNext.mPtr);
      Dynamic  getNext = iterator->__Field(HX_CSTRING("next"),true);
      CPPIA_CHECK(getNext.mPtr);

      #ifdef JUMP_BREAK
      jmp_buf here;
      jmp_buf *old = ctx->loopJumpBuf;
      ctx->loopJumpBuf = &here; \
      if (setjmp(here))
      {
          ctx->loopJumpBuf = old;
          return;
      }
      #endif
 

      while(hasNext())
      {
         switch(var.expressionType)
         {
            case etInt: *(int *)(ctx->frame+var.stackPos) = getNext(); break;
            case etFloat: *(Float *)(ctx->frame+var.stackPos) = getNext(); break;
            case etString: *(String *)(ctx->frame+var.stackPos) = getNext(); break;
            case etObject: *(hx::Object **)(ctx->frame+var.stackPos) = getNext().mPtr; break;
            case etVoid:
            case etNull:
               break;
         }

         try
         {
            loop->runVoid(ctx);
         }
         catch( LoopBreak * )
         {
            break;
         }
         catch( LoopContinue * )
         {
            // fallthrough
         }
      }
   }
};

struct WhileExpr : public CppiaVoidExpr
{
   bool      isWhileDo;
   CppiaExpr *condition;
   CppiaExpr *loop;

 
   WhileExpr(CppiaStream &stream)
   {
      isWhileDo = stream.getInt();
      condition = createCppiaExpr(stream);
      loop = createCppiaExpr(stream);
   }

   const char *getName() { return "WhileExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      condition = condition->link(inData);
      loop = loop->link(inData);
      return this;
   }

   void runVoid(CppiaCtx *ctx)
   {
      if (isWhileDo && !condition->runInt(ctx))
         return;

      #ifdef FLAG_BREAK
      BCR_VCHECK;
      #endif

      #ifdef JUMP_BREAK
      jmp_buf here;
      jmp_buf *old = ctx->loopJumpBuf;
      ctx->loopJumpBuf = &here; \
      if (setjmp(here))
      {
          ctx->loopJumpBuf = old;
          return;
      }
      #endif

 
      while(true)
      {
         #ifdef JUMP_BREAK
         try
         {
            loop->runVoid(ctx);
         }
         catch( LoopBreak * )
         {
            break;
         }
         catch( LoopContinue * )
         {
            // fallthrough
         }
         #else
         loop->runVoid(ctx);
         if (ctx->breakContReturn)
         {
            if (ctx->breakContReturn & (bcrBreak|bcrReturn))
               break;
            // Continue
            ctx->breakContReturn = 0;
         }
         #endif

         if ( !condition->runInt(ctx) || ctx->breakContReturn)
            break;
      }
      #ifdef FLAG_BREAK
      ctx->breakContReturn &= ~bcrLoop;
      #endif
   }
};

struct SwitchExpr : public CppiaExpr
{
   struct Case
   {
      Expressions conditions;
      CppiaExpr   *body;
   };
   int       caseCount;
   CppiaExpr *condition;
   std::vector<Case> cases;
   CppiaExpr *defaultCase;

 
   SwitchExpr(CppiaStream &stream)
   {
      caseCount = stream.getInt();
      bool hasDefault = stream.getInt();
      condition = createCppiaExpr(stream);
      cases.resize(caseCount);
      for(int i=0;i<caseCount;i++)
      {
         int count = stream.getInt();
         ReadExpressions(cases[i].conditions,stream,count);
         cases[i].body = createCppiaExpr(stream);
      }
      defaultCase = hasDefault ? createCppiaExpr(stream) : 0;
   }

   const char *getName() { return "SwitchExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      condition = condition->link(inData);
      for(int i=0;i<caseCount;i++)
      {
         LinkExpressions(cases[i].conditions,inData);
         cases[i].body = cases[i].body->link(inData);
      }
      if (defaultCase)
         defaultCase = defaultCase->link(inData);
      return this;
   }

   template<typename T>
   CppiaExpr *TGetBody(CppiaCtx *ctx)
   {
      T value;
      runValue(value,ctx,condition);
      for(int i=0;i<caseCount;i++)
      {
         Case &c = cases[i];
         for(int j=0;j<c.conditions.size();j++)
         {
            T caseVal;
            runValue(caseVal,ctx,c.conditions[j]);
            if (value==caseVal)
               return c.body;
         }
      }
      return defaultCase;
   }


   CppiaExpr *getBody(CppiaCtx *ctx)
   {
      switch(condition->getType())
      {
         case etInt :
             // todo - int/map ?
             return TGetBody<int>(ctx);
         case etString : return TGetBody<String>(ctx);
         case etFloat : return TGetBody<Float>(ctx);
         // Enum
         case etObject : return TGetBody<Dynamic>(ctx);
         default: ;
      }

     return defaultCase;
   }

   void runVoid(CppiaCtx *ctx)
   {
      CppiaExpr *body = getBody(ctx);
      if (body)
         body->runVoid(ctx);
   }
   int runInt(CppiaCtx *ctx)
   {
      CppiaExpr *body = getBody(ctx);
      if (body)
         return body->runInt(ctx);
      return 0;
   }

   Float runFloat(CppiaCtx *ctx)
   {
      CppiaExpr *body = getBody(ctx);
      if (body)
         return body->runFloat(ctx);
      return 0;
   }

   ::String    runString(CppiaCtx *ctx)
    {
      CppiaExpr *body = getBody(ctx);
      if (body)
         return body->runString(ctx);
      return String();
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      CppiaExpr *body = getBody(ctx);
      if (body)
         return body->runObject(ctx);
      return 0;
   }

};


struct TryExpr : public CppiaVoidExpr
{
   struct Catch
   {
      CppiaStackVar var;
      TypeData    *type;
      CppiaExpr   *body;
   };
   int       catchCount;
   CppiaExpr *body;
   std::vector<Catch> catches;

 
   TryExpr(CppiaStream &stream)
   {
      catchCount = stream.getInt();
      body = createCppiaExpr(stream);
      catches.resize(catchCount);
      for(int i=0;i<catchCount;i++)
      {
         catches[i].var.fromStream(stream);
         catches[i].body = createCppiaExpr(stream);
      }
   }

   const char *getName() { return "TryExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      body = body->link(inData);
      for(int i=0;i<catchCount;i++)
      {
         Catch &c = catches[i];
         c.var.link(inData);
         c.type = inData.types[c.var.typeId];
         c.body = c.body->link(inData);
      }
      return this;
   }
   // TODO - return types...
   void runVoid(CppiaCtx *ctx)
   {
      try
      {
         body->runVoid(ctx);
      }
      catch(Dynamic caught)
      {
         //Class cls = caught->__GetClass();
         for(int i=0;i<catchCount;i++)
         {
            Catch &c = catches[i];
            if ( c.type->isClassOf(caught) )
            {
               HX_STACK_BEGIN_CATCH
               c.var.set(ctx,caught);
               c.body->runVoid(ctx);
               return;
            }
         }
         HX_STACK_DO_THROW(caught);
      }
   }
};


struct VarRef : public CppiaExpr
{
   int varId;
   CppiaStackVar *var;
   ExprType type;
   int      stackPos;

   VarRef(CppiaStream &stream)
   {
      varId = stream.getInt();
      var = 0;
      type = etVoid;
      stackPos = 0;
   }

   ExprType getType() { return type; }

   const char *getName() { return "VarRef"; }
   CppiaExpr *link(CppiaData &inData)
   {
      var = inData.layout->findVar(varId);
      if (!var)
      {
         // link to cppia static...

         printf("Could not link var %d %s.\n", varId, inData.strings[ sVarIdNameMap[varId] ].__s );
         inData.layout->dump(inData.strings,"");
         inData.where(this);
         throw "Unknown variable";
      }

      stackPos = var->stackPos;
      type = var->expressionType;

      CppiaExpr *replace = 0;
      switch(type)
      {
         case etInt:
            replace = new MemReference<int,locStack>(this,var->stackPos);
            break;
         case etFloat:
            replace = new MemReference<Float,locStack>(this,var->stackPos);
            break;
         case etString:
            replace = new MemReference<String,locStack>(this,var->stackPos);
            break;
         case etObject:
            replace = new MemReference<hx::Object *,locStack>(this,var->stackPos);
            break;
         case etVoid:
         case etNull:
            break;
      }
      if (replace)
      {
         replace->link(inData);
         delete this;
         return replace;
      }

      return this;
   }
};


struct JumpBreak : public CppiaVoidExpr
{
   JumpBreak() {  }
   void runVoid(CppiaCtx *ctx)
   {
      ctx->breakJump();
   }
   const char *getName() { return "JumpBreak"; }

};

struct FlagBreak : public CppiaVoidExpr
{
   FlagBreak() {  }
   void runVoid(CppiaCtx *ctx)
   {
      CPPIA_CHECK(0);
      ctx->breakFlag();
   }
   const char *getName() { return "FlagBreak"; }

};




template<typename T>
struct ThrowType : public CppiaVoidExpr
{
   T* value;

   ThrowType(T *inValue=0) { value = inValue; }
   void runVoid(CppiaCtx *ctx)
   {
      throw value;
   }
   const char *getName() { return "Break/Continue"; }

};

struct RetVal : public CppiaVoidExpr
{
   bool      retVal;
   CppiaExpr *value;
   ExprType  returnType;

   RetVal(CppiaStream &stream,bool inRetVal)
   {
      retVal = inRetVal;
      if (retVal)
      {
         int t = stream.getInt();
         value = createCppiaExpr(stream);
         value->haxeTypeId = t;
      }
      else
         value = 0;
   }

   const char *getName() { return "RetVal"; }
   CppiaExpr *link(CppiaData &inData)
   {
      if (value)
      {
         value = value->link(inData);
         returnType = inData.layout->returnType;
      }
      else
         returnType = etNull;
      return this;
   }

   void runVoid(CppiaCtx *ctx)
   {
      #ifdef DEBUG_RETURN_TYPE
      gLastRet = returnType;
      #endif
      switch(returnType)
      {
         case etInt:
            ctx->returnInt( value->runInt(ctx) );
            ctx->longJump();
         case etFloat:
            ctx->returnFloat( value->runFloat(ctx) );
            ctx->longJump();
         case etString:
            ctx->returnString( value->runString(ctx) );
            ctx->longJump();
         case etObject:
            ctx->returnObject( value->runObject(ctx) );
            ctx->longJump();
         default:
            if (value)
               value->runVoid(ctx);
      }
      ctx->longJump();
   }
};



struct BinOp : public CppiaExpr
{
   CppiaExpr *left;
   CppiaExpr *right;
   ExprType type;

   BinOp(CppiaStream &stream)
   {
      left = createCppiaExpr(stream);
      right = createCppiaExpr(stream);
      type = etFloat;
   }

   ExprType getType() { return type; }

   const char *getName() { return "BinOp"; }
   CppiaExpr *link(CppiaData &inData)
   {
      left = left->link(inData);
      right = right->link(inData);

      if (left->getType()==etInt && right->getType()==etInt)
         type = etInt;
      else
         type = etFloat;
      return this;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      if (type==etInt)
         return Dynamic(runInt(ctx)).mPtr;
      return Dynamic(runFloat(ctx)).mPtr;
   }
   String runString(CppiaCtx *ctx)
   {
      if (type==etInt)
         return String(runInt(ctx));
      return String(runFloat(ctx));
   }
};


#define ARITH_OP(name,OP) \
struct name : public BinOp \
{ \
   name(CppiaStream &stream) : BinOp(stream) { } \
 \
   int runInt(CppiaCtx *ctx) \
   { \
      int lval = left->runInt(ctx); \
      return lval OP right->runInt(ctx); \
   } \
   Float runFloat(CppiaCtx *ctx) \
   { \
      Float lval = left->runFloat(ctx); \
      return lval OP right->runFloat(ctx); \
   } \
};

struct OpDiv : public BinOp
{
   OpDiv(CppiaStream &stream) : BinOp(stream) { }

   ExprType getType() { return etFloat; }
   int runInt(CppiaCtx *ctx)
   {
      int lval = left->runInt(ctx);
      return lval / right->runInt(ctx);
   }
   Float runFloat(CppiaCtx *ctx)
   {
      Float lval = left->runFloat(ctx);
      return lval / right->runFloat(ctx);
   }
};


ARITH_OP(OpMult,*)
ARITH_OP(OpSub,-)


struct ThrowExpr : public CppiaVoidExpr
{
   CppiaExpr *value;
   ThrowExpr(CppiaStream &stream)
   {
      value = createCppiaExpr(stream);
   }
   const char *getName() { return "ThrowExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      value = value->link(inData);
      return this;
   }
   void runVoid(CppiaCtx *ctx)
   {
      throw Dynamic( value->runObject(ctx) );
   }
};

struct OpNot : public CppiaBoolExpr
{
   CppiaExpr *value;
   OpNot(CppiaStream &stream)
   {
      value = createCppiaExpr(stream);
   }
   CppiaExpr *link(CppiaData &inData)
   {
      value = value->link(inData);
      return this;
   }
   int runInt(CppiaCtx *ctx)
   {
      return ! value->runInt(ctx);
   }
};

struct OpAnd : public CppiaBoolExpr
{
   CppiaExpr *left;
   CppiaExpr *right;
   OpAnd(CppiaStream &stream)
   {
      left = createCppiaExpr(stream);
      right = createCppiaExpr(stream);
   }
   CppiaExpr *link(CppiaData &inData)
   {
      left = left->link(inData);
      right = right->link(inData);
      return this;
   }
   int runInt(CppiaCtx *ctx)
   {
      return left->runInt(ctx) && right->runInt(ctx);
   }
};


struct OpOr : public OpAnd
{
   OpOr(CppiaStream &stream) : OpAnd(stream) { }
   int runInt(CppiaCtx *ctx)
   {
      return left->runInt(ctx) || right->runInt(ctx);
   }
};




struct BitNot : public CppiaIntExpr
{
   CppiaExpr *left;
   BitNot(CppiaStream &stream)
   {
      left = createCppiaExpr(stream);
   }
   CppiaExpr *link(CppiaData &inData)
   {
      left = left->link(inData);
      return this;
   }
   int runInt(CppiaCtx *ctx)
   {
      return ~left->runInt(ctx);
   }
};

struct BitAnd : public CppiaIntExpr
{
   CppiaExpr *left;
   CppiaExpr *right;
   BitAnd(CppiaStream &stream)
   {
      left = createCppiaExpr(stream);
      right = createCppiaExpr(stream);
   }
   const char *getName() { return "BitAnd"; }
   CppiaExpr *link(CppiaData &inData)
   {
      left = left->link(inData);
      right = right->link(inData);
      return this;
   }
   int runInt(CppiaCtx *ctx)
   {
      int l = left->runInt(ctx);
      return l & right->runInt(ctx);
   }
};

struct BitOr : public BitAnd
{
   BitOr(CppiaStream &stream) : BitAnd(stream) { }
   int runInt(CppiaCtx *ctx)
   {
      int l = left->runInt(ctx);
      return l | right->runInt(ctx);
   }
};


struct BitXOr : public BitAnd
{
   BitXOr(CppiaStream &stream) : BitAnd(stream) { }
   int runInt(CppiaCtx *ctx)
   {
      int l = left->runInt(ctx);
      return l ^ right->runInt(ctx);
   }
};


struct BitUSR : public BitAnd
{
   BitUSR(CppiaStream &stream) : BitAnd(stream) { }
   int runInt(CppiaCtx *ctx)
   {
      int l = left->runInt(ctx);
      return  hx::UShr(l , right->runInt(ctx));
   }
};


struct BitShiftR : public BitAnd
{
   BitShiftR(CppiaStream &stream) : BitAnd(stream) { }
   int runInt(CppiaCtx *ctx)
   {
      int l = left->runInt(ctx);
      return  l >> right->runInt(ctx);
   }
};


struct BitShiftL : public BitAnd
{
   BitShiftL(CppiaStream &stream) : BitAnd(stream) { }
   int runInt(CppiaCtx *ctx)
   {
      int l = left->runInt(ctx);
      return  l << right->runInt(ctx);
   }
};



template<bool AS_DYNAMIC>
struct SpecialAdd : public CppiaExpr
{
   CppiaExpr *left;
   CppiaExpr *right;

   SpecialAdd(const CppiaExpr *inSrc, CppiaExpr *inLeft, CppiaExpr *inRight)
      : CppiaExpr(inSrc)
   {
      left = inLeft;
      right = inRight;
   }
   void runVoid(CppiaCtx *ctx)
   {
      left->runVoid(ctx);
      right->runVoid(ctx);
   }
   String runString(CppiaCtx *ctx)
   {
      if (AS_DYNAMIC)
      {
         Dynamic lval = left->runObject(ctx);
         return lval + Dynamic(right->runObject(ctx));
      }
      else
      {
         String lval = left->runString(ctx);
         return lval + right->runString(ctx);
      }
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      if (AS_DYNAMIC)
      {
         Dynamic lval = left->runObject(ctx);
         return (lval + Dynamic(right->runObject(ctx))).mPtr;
      }
 
      return Dynamic(runString(ctx)).mPtr;
   }
   int runInt(CppiaCtx *ctx)
   {
      if (AS_DYNAMIC)
      {
         Dynamic lval = left->runObject(ctx);
         return (lval + Dynamic(right->runObject(ctx)))->__ToInt();
      }
 
      left->runVoid(ctx);
      right->runVoid(ctx);
      return 0;
   }
   Float runFloat(CppiaCtx *ctx)
   {
      if (AS_DYNAMIC)
      {
         Dynamic lval = left->runObject(ctx);
         return (lval + Dynamic(right->runObject(ctx)))->__ToDouble();
      }
 
      left->runVoid(ctx);
      right->runVoid(ctx);
      return 0;
   }
};

struct OpNeg : public CppiaExpr
{
   CppiaExpr *value;
   ExprType type;
   OpNeg(CppiaStream &stream)
   {
      value = createCppiaExpr(stream);
   }

   CppiaExpr *link(CppiaData &inData)
   {
      value = value->link(inData);
      type = value->getType()==etInt ? etInt : etFloat;
      return this;
   }

   ExprType getType() { return type; }

   void runVoid(CppiaCtx *ctx) { value->runVoid(ctx); }
   int runInt(CppiaCtx *ctx)
   {
      return - value->runInt(ctx);
   }
   Float runFloat(CppiaCtx *ctx)
   {
      return - value->runFloat(ctx);
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      return Dynamic(- value->runFloat(ctx)).mPtr;
   }
};

struct OpAdd : public BinOp
{
   OpAdd(CppiaStream &stream) : BinOp(stream)
   {
   }

   CppiaExpr *link(CppiaData &inData)
   {
      BinOp::link(inData);

      if (left->getType()==etString || right->getType()==etString )
      {
         CppiaExpr *result = new SpecialAdd<false>(this,left,right);
         delete this;
         return result;
      }
      else if ( !isNumeric(left->getType()) || !isNumeric(right->getType()) )
      {
         CppiaExpr *result = new SpecialAdd<true>(this,left,right);
         delete this;
         return result;
      }

      return this;
   }

   void runVoid(CppiaCtx *ctx)
   {
      left->runVoid(ctx);
      right->runVoid(ctx);
   }
   int runInt(CppiaCtx *ctx)
   {
      int lval = left->runInt(ctx);
      return lval + right->runInt(ctx);
   }
   Float runFloat(CppiaCtx *ctx)
   {
      Float lval = left->runFloat(ctx);
      return lval + right->runFloat(ctx);
   }
};


struct OpMod : public BinOp
{
   OpMod(CppiaStream &stream) : BinOp(stream)
   {
   }


   int runInt(CppiaCtx *ctx)
   {
      int lval = left->runInt(ctx);
      return lval % right->runInt(ctx);
   }
   Float runFloat(CppiaCtx *ctx)
   {
      Float lval = left->runFloat(ctx);
      return hx::DoubleMod(lval,right->runFloat(ctx));
   }
};


struct EnumField : public CppiaDynamicExpr
{
   int                  enumId;
   int                  fieldId;
   CppiaEnumConstructor *value;
   Expressions          args;

   // Mark class?
   String               enumName;
   Class                enumClass;

   EnumField(CppiaStream &stream,bool inWithArgs)
   {
      enumId = stream.getInt();
      fieldId = stream.getInt();
      value= 0;
      if (inWithArgs)
      {
         int argCount = stream.getInt();
         for(int a=0;a<argCount;a++)
            args.push_back( createCppiaExpr(stream) );
      }
   }

   const char *getName() { return "EnumField"; }
   CppiaExpr *link(CppiaData &inData)
   {
      TypeData *type = inData.types[enumId];
      if (type->cppiaClass)
      {
         if (!type->cppiaClass->isEnum)
            throw "Field of non-enum";
         value = type->cppiaClass->findEnum(fieldId);
      }
      else
      {
         enumClass = Class_obj::Resolve(type->name);
         if (!enumClass.mPtr)
         {
            printf("Could not find enum %s\n", type->name.__s );
            throw "Bad enum";
         }
         enumName = inData.strings[fieldId];
         inData.markable.push_back(this);
      }

      LinkExpressions(args,inData);
      return this;
   }

   hx::Object *runObject(CppiaCtx *ctx)
   {
      int s = args.size();
      if (s==0)
         return value ? value->value.mPtr : enumClass->ConstructEnum(enumName,null()).mPtr;

      Array<Dynamic> dynArgs = Array_obj<Dynamic>::__new(s,s);
      for(int a=0;a<s;a++)
         dynArgs[a] = args[a]->runObject(ctx);

      return value ? value->create(dynArgs) : enumClass->ConstructEnum(enumName,dynArgs).mPtr;
   }

   void mark(hx::MarkContext *__inCtx)
   {
      HX_MARK_MEMBER(enumName);
      HX_MARK_MEMBER(enumClass);
   }
   void visit(hx::VisitContext *__inCtx)
   {
      HX_VISIT_MEMBER(enumName);
      HX_VISIT_MEMBER(enumClass);
   }

};

struct CrementExpr : public CppiaExpr 
{
   CrementOp op;
   CppiaExpr *lvalue;

   CrementExpr(CppiaStream &stream,CrementOp inOp)
   {
      op = inOp;
      lvalue = createCppiaExpr(stream);
   }
   const char *getName() { return "CrementExpr"; }
   CppiaExpr *link(CppiaData &inData)
   {
      lvalue = lvalue->link(inData);
      CppiaExpr *replace = lvalue->makeCrement( op );
      if (!replace)
      {
         printf("Could not create increment operator\n");
         inData.where(lvalue);
         throw "Bad increment";
      }
      replace->link(inData);
      delete this;
      return replace;
   }
};

struct OpCompareBase : public CppiaBoolExpr 
{
   enum CompareType { compFloat, compInt, compString, compDynamic };

   CppiaExpr *left;
   CppiaExpr *right;
   CompareType compareType;

   OpCompareBase(CppiaStream &stream)
   {
      left = createCppiaExpr(stream);
      right = createCppiaExpr(stream);
      compareType = compDynamic;
   }

   const char *getName() { return "OpCompare"; }

   CppiaExpr *link(CppiaData &inData)
   {
      left = left->link(inData);
      right = right->link(inData);
      ExprType t1 = left->getType();
      ExprType t2 = right->getType();

      if (isNumeric(t1) || isNumeric(t2))
      {
         compareType = t1==etInt && t2==etInt ? compInt : compFloat;
      }
      else if (t1==etString || t2==etString)
      {
         compareType = compString;
      }
      else
         compareType = compDynamic;

      return this;
   }
};

template<typename COMPARE>
struct OpCompare : public OpCompareBase 
{
   COMPARE compare;

   OpCompare(CppiaStream &stream)
      : OpCompareBase(stream) { }

   int runInt(CppiaCtx *ctx)
   {
      switch(compareType)
      {
         case compFloat:
         {
            float leftVal = left->runFloat(ctx);
            float rightVal = right->runFloat(ctx);
            return compare.test(leftVal,rightVal);
         }
         case compInt:
         {
            int leftVal = left->runInt(ctx);
            int rightVal = right->runInt(ctx);
            return compare.test(leftVal,rightVal);
         }
         case compString:
         {
            String leftVal = left->runString(ctx);
            String rightVal = right->runString(ctx);
            return compare.test(leftVal,rightVal);
         }
         case compDynamic:
         {
            Dynamic leftVal(left->runObject(ctx));
            Dynamic rightVal(right->runObject(ctx));
            return compare.test(leftVal,rightVal);
         }
      }

      return 0;
   }
   hx::Object *runObject(CppiaCtx *ctx)
   {
      return Dynamic( (bool)runInt(ctx) ).mPtr;
   }
};


#define DEFINE_COMPARE_OP(name,OP) \
struct name \
{ \
   template<typename T> \
   inline bool test(const T &left, const T&right) \
   { \
      return left OP right; \
   } \
};

DEFINE_COMPARE_OP(CompareLess,<);
DEFINE_COMPARE_OP(CompareLessEq,<=);
DEFINE_COMPARE_OP(CompareGreater,>);
DEFINE_COMPARE_OP(CompareGreaterEq,>=);
DEFINE_COMPARE_OP(CompareEqual,==);
DEFINE_COMPARE_OP(CompareNotEqual,!=);



CppiaExpr *createCppiaExpr(CppiaStream &stream)
{
   int fileId = stream.getInt();
   int line = stream.getInt();
   std::string tok = stream.getToken();
   //DBGLOG(" expr %s\n", tok.c_str() );

   CppiaExpr *result = 0;
   if (tok=="FUN")
      result = new ScriptCallable(stream);
   else if (tok=="BLOCK")
      result = new BlockExpr(stream);
   else if (tok=="IFELSE")
      result = new IfElseExpr(stream);
   else if (tok=="IF")
      result = new IfExpr(stream);
   else if (tok=="ISNULL")
      result = new IsNull(stream);
   else if (tok=="NOTNULL")
      result = new IsNotNull(stream);
   else if (tok=="CALLSTATIC")
      result = new CallStatic(stream);
   else if (tok=="CALLTHIS")
      result = new CallMember(stream,callThis);
   else if (tok=="CALLSUPERNEW")
      result = new CallMember(stream,callSuperNew);
   else if (tok=="CALLSUPER")
      result = new CallMember(stream,callSuper);
   else if (tok=="CALLMEMBER")
      result = new CallMember(stream,callObject);
   else if (tok=="CALLGLOBAL")
      result = new CallGlobal(stream);
   else if (tok=="true")
      result = new DataVal<bool>(true);
   else if (tok=="false")
      result = new DataVal<bool>(false);
   else if (tok[0]=='s')
      result = new StringVal(atoi(tok.c_str()+1));
   else if (tok[0]=='f')
      result = new DataVal<Float>(atof(tok.c_str()+1));
   else if (tok[0]=='i')
      result = new DataVal<int>(atoi(tok.c_str()+1));
   else if (tok=="POSINFO")
      result = new PosInfo(stream);
   else if (tok=="VAR")
      result = new VarRef(stream);
   else if (tok=="WHILE")
      result = new WhileExpr(stream);
   else if (tok=="FOR")
      result = new ForExpr(stream);
   else if (tok=="RETVAL")
      result = new RetVal(stream,true);
   else if (tok=="RETURN")
      result = new RetVal(stream,false);
   else if (tok=="THROW")
      result = new ThrowExpr(stream);
   else if (tok=="CALL")
      result = new Call(stream);
   else if (tok=="FLINK")
      result = new GetFieldByLinkage(stream,false);
   else if (tok=="THIS")
      result = new ThisExpr(stream);
   else if (tok=="CLASSOF")
      result = new ClassOfExpr(stream);
   else if (tok=="FTHISINST")
      result = new GetFieldByLinkage(stream,true);
   else if (tok=="FNAME")
      result = new GetFieldByName(stream,false);
   else if (tok=="FSTATIC")
      result = new GetFieldByName(stream,false,true);
   else if (tok=="FTHISNAME")
      result = new GetFieldByName(stream,true);
   else if (tok=="FENUM")
      result = new EnumField(stream,false);
   else if (tok=="CREATEENUM")
      result = new EnumField(stream,true);
   else if (tok=="NULL")
      result = new NullVal();
   else if (tok=="TVARS")
      result = new TVars(stream);
   else if (tok=="NEW")
      result = new NewExpr(stream);
   else if (tok=="ARRAYI")
      result = new ArrayIExpr(stream);
   else if (tok=="ENUMI")
      result = new EnumIExpr(stream);
   else if (tok=="ADEF")
      result = new ArrayDef(stream);
   else if (tok=="OBJDEF")
      result = new ObjectDef(stream);
   else if (tok=="TOINTERFACE")
      result = new ToInterface(stream,false);
   else if (tok=="TOINTERFACEARRAY")
      result = new ToInterface(stream,true);
   else if (tok=="CAST")
      result = new CastExpr(stream,castNOP);
   else if (tok=="TODYNARRAY")
      result = new CastExpr(stream,castDynArray);
   else if (tok=="TODATAARRAY")
      result = new CastExpr(stream,castDataArray);
   else if (tok=="SWITCH")
      result = new SwitchExpr(stream);
   else if (tok=="TRY")
      result = new TryExpr(stream);
   else if (tok=="BREAK")
      #ifdef FLAG_BREAK
      result = new FlagBreak();
      #elif defined(JUMP_BREAK)
      result = new JumpBreak();
      #else
      result = new ThrowType<LoopBreak>(&sLoopBreak);
      #endif
   else if (tok=="CONTINUE")
      result = new ThrowType<LoopContinue>(&sLoopContinue);
   // Uniops..
   else if (tok=="NEG")
      result = new OpNeg(stream);
   else if (tok=="++")
      result = new CrementExpr(stream,coPreInc);
   else if (tok=="+++")
      result = new CrementExpr(stream,coPostInc);
   else if (tok=="--")
      result = new CrementExpr(stream,coPreDec);
   else if (tok=="---")
      result = new CrementExpr(stream,coPostDec);
   // Arithmetic
   else if (tok=="+")
      result = new OpAdd(stream);
   else if (tok=="*")
      result = new OpMult(stream);
   else if (tok=="/")
      result = new OpDiv(stream);
   else if (tok=="-")
      result = new OpSub(stream);
   else if (tok=="&")
      result = new BitAnd(stream);
   else if (tok=="|")
      result = new BitOr(stream);
   else if (tok=="^")
      result = new BitXOr(stream);
   else if (tok=="<<")
      result = new BitShiftL(stream);
   else if (tok==">>")
      result = new BitShiftR(stream);
   else if (tok==">>>")
      result = new BitUSR(stream);
   else if (tok=="%")
      result = new OpMod(stream);
   // Arithmetic =
   else if (tok=="SET")
      result = new SetExpr(stream,aoSet);
   else if (tok=="+=")
      result = new SetExpr(stream,aoAdd);
   else if (tok=="*=")
      result = new SetExpr(stream,aoMult);
   else if (tok=="/=")
      result = new SetExpr(stream,aoDiv);
   else if (tok=="-=")
      result = new SetExpr(stream,aoSub);
   else if (tok=="&=")
      result = new SetExpr(stream,aoAnd);
   else if (tok=="|=")
      result = new SetExpr(stream,aoOr);
   else if (tok=="^=")
      result = new SetExpr(stream,aoXOr);
   else if (tok=="<<=")
      result = new SetExpr(stream,aoShl);
   else if (tok==">>=")
      result = new SetExpr(stream,aoShr);
   else if (tok==">>>=")
      result = new SetExpr(stream,aoUShr);
   else if (tok=="%=")
      result = new SetExpr(stream,aoMod);
   // Comparison
   else if (tok=="<")
      result = new OpCompare<CompareLess>(stream);
   else if (tok=="<=")
      result = new OpCompare<CompareLessEq>(stream);
   else if (tok==">")
      result = new OpCompare<CompareGreater>(stream);
   else if (tok==">=")
      result = new OpCompare<CompareGreaterEq>(stream);
   else if (tok=="==")
      result = new OpCompare<CompareEqual>(stream);
   else if (tok=="!=")
      result = new OpCompare<CompareNotEqual>(stream);
   // Logical
   else if (tok=="!")
      result = new OpNot(stream);
   else if (tok=="&&")
      result = new OpAnd(stream);
   else if (tok=="||")
      result = new OpOr(stream);
   else if (tok=="~")
      result = new BitNot(stream);

   if (!result)
      throw "invalid expression";

   stream.cppiaData->setDebug(result, fileId, line);


   return result;
}

// --- TypeData -------------------------

void TypeData::link(CppiaData &inData)
{
   if (linked)
      return;

   linked = true;

   TypeData *cppiaSuperType = 0;
   if (cppiaClass && cppiaClass->superId)
   {
     cppiaSuperType = inData.types[cppiaClass->superId];
     cppiaSuperType->link(inData);
   }

   if (name.length>0)
   {
      haxeClass = Class_obj::Resolve(name);
      DBGLOG("Link %s, haxe=%s\n", name.__s, haxeClass.mPtr ? haxeClass.mPtr->mName.__s : "?" );
      if (!haxeClass.mPtr && !cppiaClass && name==HX_CSTRING("int"))
      {
         name = HX_CSTRING("Int");
         haxeClass = Class_obj::Resolve(name);
      }
      if (!haxeClass.mPtr && !cppiaClass && name==HX_CSTRING("bool"))
      {
         name = HX_CSTRING("Bool");
         haxeClass = Class_obj::Resolve(name);
      }
      if (!haxeClass.mPtr && !cppiaClass && (name==HX_CSTRING("float") || name==HX_CSTRING("double")))
      {
         name = HX_CSTRING("Float");
         haxeClass = Class_obj::Resolve(name);
      }

      DBGLOG(" link type '%s' %s ", name.__s, haxeClass.mPtr ? "native" : "script" );
      interfaceBase = sScriptRegisteredInterface ? (*sScriptRegisteredInterface)[name.__s] : 0;
      isInterface = interfaceBase || (cppiaClass && cppiaClass->isInterface);

      if (!haxeClass.mPtr && name.substr(0,6)==HX_CSTRING("Array.") || name==HX_CSTRING("Array") )
      {
         haxeClass = Class_obj::Resolve(HX_CSTRING("Array"));
         if (name.length==5)
            arrayType = arrAny;
         else
         {
            String t = name.substr(6,null());

            if (t==HX_CSTRING("int"))
               arrayType = arrInt;
            else if (t==HX_CSTRING("bool"))
               arrayType = arrBool;
            else if (t==HX_CSTRING("Float"))
               arrayType = arrFloat;
            else if (t==HX_CSTRING("String"))
               arrayType = arrString;
            else if (t==HX_CSTRING("unsigned char"))
               arrayType = arrUnsignedChar;
            else if (t==HX_CSTRING("Any"))
               arrayType = arrAny;
            else if (t==HX_CSTRING("Object"))
               arrayType = arrObject;
            else
               throw "Unknown array type";
         }

         DBGLOG("array type %d\n",arrayType);
      }
      else if (!haxeClass.mPtr && cppiaSuperType)
      {
         haxeBase = cppiaSuperType->haxeBase;
         haxeClass.mPtr = cppiaSuperType->haxeClass.mPtr;
         DBGLOG("extends %s\n", cppiaSuperType->name.__s);
      }
      else if (!haxeClass.mPtr)
      {
         haxeBase = isInterface ? 0 : (*sScriptRegistered)["hx.Object"];

         if (!isInterface && (*sScriptRegistered)[name.__s])
            throw "New class, but with existing def";

         DBGLOG(isInterface ? "interface base\n" : "base\n");
      }
      else
      {
         haxeBase = (*sScriptRegistered)[name.__s];
         if (!haxeBase)
         {
            DBGLOG("assumed base (todo - register)\n");
            haxeBase = (*sScriptRegistered)["hx.Object"];
         }
         else
         {
            DBGLOG("\n");
         }
      }

      if (name==HX_CSTRING("Void"))
         expressionType = etVoid;
      else if (name==HX_CSTRING("Null"))
         expressionType = etNull;
      else if (name==HX_CSTRING("String"))
         expressionType = etString;
      else if (name==HX_CSTRING("Float"))
         expressionType = etFloat;
      else if (name==HX_CSTRING("Int") || name==HX_CSTRING("Bool"))
         expressionType = etInt;
      else
         expressionType = etObject;
   }
   else
   {
      haxeBase = (*sScriptRegistered)["hx.Object"];
      haxeClass = Class_obj::Resolve(HX_CSTRING("Dynamic"));
   }
}

// --- CppiaData -------------------------

CppiaData::~CppiaData()
{
   delete main;
   for(int i=0;i<classes.size();i++)
      delete classes[i];
}

void CppiaData::link()
{
   DBGLOG("Resolve registered - super\n");
   ScriptRegistered *hxObj = (*sScriptRegistered)["hx.Object"];
   for(ScriptRegisteredMap::iterator i = sScriptRegistered->begin(); i!=sScriptRegistered->end();++i)
   {
      DBGLOG("== %s ==\n", i->first.c_str());
      if (i->second == 0)
         i->second = hxObj;
      if (i->second == hxObj)
      {
         DBGLOG(" super =0\n");
         continue;
      }
      Class cls = Class_obj::Resolve( String(i->first.c_str() ) );
      if (cls.mPtr)
      {
         Class superClass = cls->GetSuper();
         if (superClass.mPtr)
         {
            ScriptRegistered *superRef = (*sScriptRegistered)[superClass.mPtr->mName.__s];
            if (superRef)
            {
               DBGLOG("registered %s\n",superClass.mPtr->mName.__s);
               i->second->haxeSuper = superRef;
            }
         }
      }

      DBGLOG("haxe super = %p\n", i->second->haxeSuper);
      if (!i->second->haxeSuper)
      {
         DBGLOG("using hx.Object\n");
         i->second->haxeSuper = hxObj;
      }
   }

   DBGLOG("Resolve typeIds\n");
   for(int t=0;t<types.size();t++)
      types[t]->link(*this);

   DBGLOG("Resolve inherited atributes\n");
   for(int i=0;i<classes.size();i++)
   {
      classes[i]->linkTypes();
   }

   for(int i=0;i<classes.size();i++)
   {
      linkingClass = classes[i];
      classes[i]->link();
   }
   linkingClass = 0;

   if (main)
      main = main->link(*this);
}

/*
CppiaClassInfo *CppiaData::findClass(String inName)
{
   for(int i=0;i<classes.size();i++)
      if (strings[classes[i]->nameId] == inName)
         return classes[i];
   return 0;
}
*/


void CppiaData::mark(hx::MarkContext *__inCtx)
{
   DBGLOG(" --- MARK --- \n");
   HX_MARK_MEMBER(strings);
   for(int i=0;i<types.size();i++)
   {
      types[i]->mark(__inCtx);
   }
   for(int i=0;i<markable.size();i++)
   {
      markable[i]->mark(__inCtx);
   }
   for(int i=0;i<classes.size();i++)
      if (classes[i])
      {
         classes[i]->mark(__inCtx);
      }
}

void CppiaData::visit(hx::VisitContext *__inCtx)
{
   HX_VISIT_MEMBER(strings);
   for(int i=0;i<types.size();i++)
      types[i]->visit(__inCtx);
   for(int i=0;i<markable.size();i++)
      markable[i]->visit(__inCtx);
   for(int i=0;i<classes.size();i++)
      if (classes[i])
         classes[i]->visit(__inCtx);
}

// TODO  - more than one
static hx::Object *currentCppia = 0;

std::vector<hx::Resource> scriptResources;

bool LoadCppia(String inValue)
{
   CppiaData   *cppiaPtr = new CppiaData();
   currentCppia = new CppiaObject(cppiaPtr); 
   GCAddRoot(&currentCppia);


   CppiaData   &cppia = *cppiaPtr;
   CppiaStream stream(cppiaPtr,inValue.__s, inValue.length);

   bool ok = true;
   try
   {
      std::string tok = stream.getToken();
      if (tok!="CPPIA")
         throw "Bad magic";

      int stringCount = stream.getInt();
      cppia.cStrings.resize(stringCount);
      for(int s=0;s<stringCount;s++)
      {
         cppia.strings[s] = stream.readString();
         cppia.cStrings[s] = std::string(cppia.strings[s].__s,cppia.strings[s].length);
      }

      int typeCount = stream.getInt();
      cppia.types.resize(typeCount);
      DBGLOG("Type count : %d\n", typeCount);
      for(int t=0;t<typeCount;t++)
         cppia.types[t] = new TypeData(stream.readString());

      int classCount = stream.getInt();

      cppia.classes.reserve(classCount);
      for(int c=0;c<classCount;c++)
      {
         CppiaClassInfo *info = new CppiaClassInfo(cppia);
         if (info->load(stream))
            cppia.classes.push_back(info);
      }

      tok = stream.getToken();
      if (tok=="MAIN")
      {
         DBGLOG("Main...\n");
         cppia.main = createCppiaExpr(stream);
      }
      else if (tok!="NOMAIN")
         throw "no main specified";

      tok = stream.getToken();
      if (tok=="RESOURCES")
      {
         int count = stream.getInt( );
         scriptResources.resize(count+1);
         for(int r=0;r<count;r++)
         {
            tok = stream.getToken();
            if (tok!="RESO")
               throw "no reso tag";
            
            scriptResources[r].mName = cppia.strings[stream.getInt()];
            scriptResources[r].mDataLength = stream.getInt();
         }
         stream.skipChar();
         for(int r=0;r<count;r++)
         {
            int len = scriptResources[r].mDataLength;
            unsigned char *buffer = (unsigned char *)malloc(len+5);
            *(int *)buffer = 0xffffffff;
            buffer[len+5-1] = '\0';
            stream.readBytes(buffer+4, len);
            scriptResources[r].mData = buffer + 4;
         }
         scriptResources[count].mDataLength = 0;
         scriptResources[count].mData = 0;
         scriptResources[count].mName = String();
         
         RegisterResources(&scriptResources[0]);
      }
      else
         throw "no resources tag";


   } catch(const char *error)
   {
      printf("Error reading file: %s, line %d, char %d\n", error, stream.line, stream.pos);
      ok = false;
   }

   if (ok)
      try
      {
         DBGLOG("Link...\n");
         cppia.link();
      } catch(const char *error)
      {
         printf("Error linking : %s\n", error);
         ok = false;
      }

   if (ok) try
   {
      //__hxcpp_enable(false);
      DBGLOG("--- Run --------------------------------------------\n");

      cppia.boot(&sCppiaCtx);
      if (cppia.main)
      {
         cppia.main->runVoid(&sCppiaCtx);
         //printf("Result %s.\n", cppia.main->runString(&ctx).__s);
      }
      return true;
   } catch(const char *error)
   {
      printf("Error running : %s\n", error);
   }
   return false;
}

::String ScriptableToString(void *inClass)
{
   CppiaClassInfo *info = (CppiaClassInfo *)inClass;
   return info->mClass->toString();
}

int ScriptableGetType(void *inClass)
{
   CppiaClassInfo *info = (CppiaClassInfo *)inClass;
   return info->__GetType();
}


Class ScriptableGetClass(void *inClass)
{
   CppiaClassInfo *info = (CppiaClassInfo *)inClass;
   return info->mClass;
}


// Called by haxe generated code ...
void ScriptableMark(void *inClass, hx::Object *inThis, hx::MarkContext *__inCtx)
{
   ((CppiaClassInfo *)inClass)->markInstance(inThis, __inCtx);
}

void ScriptableVisit(void *inClass, hx::Object *inThis, hx::VisitContext *__inCtx)
{
   ((CppiaClassInfo *)inClass)->visitInstance(inThis, __inCtx);
}

bool ScriptableField(hx::Object *inObj, const ::String &inName,bool inCallProp,Dynamic &outResult)
{
   void **vtable = inObj->__GetScriptVTable();
   return ((CppiaClassInfo *)vtable[-1])->getField(inObj,inName,inCallProp,outResult);
}

bool ScriptableField(hx::Object *inObj, int inName,bool inCallProp,Float &outResult)
{
   void **vtable = inObj->__GetScriptVTable();
   Dynamic result;
   if ( ((CppiaClassInfo *)vtable[-1])->getField(inObj,__hxcpp_field_from_id(inName),inCallProp,result) )
   {
      //CPPIA_CHECK(result);
      if (result.mPtr)
         outResult = result->__ToDouble();
      else
         outResult = 0;
      return true;
   }
   return false;
}

bool ScriptableField(hx::Object *inObj, int inName,bool inCallProp,Dynamic &outResult)
{
   void **vtable = inObj->__GetScriptVTable();
   return ((CppiaClassInfo *)vtable[-1])->getField(inObj,__hxcpp_field_from_id(inName),inCallProp,outResult);
}

void ScriptableGetFields(hx::Object *inObject, Array< ::String> &outFields)
{
   void **vtable = inObject->__GetScriptVTable();
   return ((CppiaClassInfo *)vtable[-1])->GetInstanceFields(inObject,outFields);
}

bool ScriptableSetField(hx::Object *inObj, const ::String &inName, Dynamic inValue,bool inCallProp, Dynamic &outResult)
{
   void **vtable = inObj->__GetScriptVTable();
   return ((CppiaClassInfo *)vtable[-1])->setField(inObj,inName,inValue,inCallProp,outResult);
}


};


void __scriptable_load_cppia(String inCode)
{
   hx::LoadCppia(inCode);
}



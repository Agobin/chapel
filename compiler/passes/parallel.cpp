//
// Transformations for begin, cobegin, and on statements
//

#include "astutil.h"
#include "expr.h"
#include "passes.h"
#include "runtime.h"
#include "stmt.h"
#include "symbol.h"
#include "stringutil.h"
#include "driver.h"
#include "files.h"


//
// Mark locals that should be heap allocated and insert a call to
// allocate them on the heap.  This is for begin blocks where the
// forked child thread and parent thread may have different lifetimes.
// The locals cannot live on a thread's stack.
//
// In addition to moving these vars to the heap, we will also use
// reference counting to garbage collect.  Will need a counter and
// mutex for each var.  Of course, those will be heap allocated also.
//
static void
heapAllocateLocals(BlockStmt* block) {
  static Map<Symbol*,Symbol*> refcMap;
  static Map<Symbol*,Symbol*> refcMutexMap;

  CallExpr* call = toCallExpr(block->body.tail);
  INT_ASSERT(call);
  for_alist(expr, block->body) {
    if (CallExpr* move = toCallExpr(expr)) {
      if (move->isPrimitive(PRIMITIVE_MOVE)) {
        if (CallExpr* ref = toCallExpr(move->get(2))) {
          if (ref->isPrimitive(PRIMITIVE_SET_REF)) {
            SymExpr* se = toSymExpr(ref->get(1));
            INT_ASSERT(se);
            VarSymbol* var = toVarSymbol(se->var);
            INT_ASSERT(var);
            DefExpr* def = var->defPoint;
            
            SymExpr* lse = toSymExpr(move->get(1));
            INT_ASSERT(lse->var->uses.n == 1);
            INT_ASSERT(lse->var->defs.n == 1);
 
            SymExpr* use = lse->var->uses.v[0];
            lse->var->defPoint->remove();
            use->var = var;
            move->remove();

            // no reference counter associated yet
            if (!refcMap.get(var)) {
              INT_ASSERT(var->type->refType);
              
              forv_Vec(SymExpr, se, var->uses) {
                if (!se->parentSymbol)
                  continue;
                CallExpr* call = toCallExpr(se->parentExpr);
                if (call && call->isPrimitive(PRIMITIVE_SET_REF)) {
                  call->replace(se->remove());
                } else if (!call || !(call->isPrimitive(PRIMITIVE_SET_MEMBER) || call->isPrimitive(PRIMITIVE_GET_MEMBER))) {
                  VarSymbol* tmp = new VarSymbol("_tmp", var->type);
                  se->getStmtExpr()->insertBefore(new DefExpr(tmp));
                  se->getStmtExpr()->insertBefore(new CallExpr(PRIMITIVE_MOVE, tmp, new CallExpr(PRIMITIVE_GET_REF, var)));
                  se->var = tmp;
                }
              }

              var->type = var->type->refType;
                    
              // create reference counter
              VarSymbol *refc = new VarSymbol(astr("_", var->name, "_refc"),
                                              dtInt[INT_SIZE_32]->refType);
              def->insertBefore(new DefExpr(refc));
              
              // create reference counter mutex
              VarSymbol *refcMutex = new VarSymbol(astr("_", var->name, "_refcmutex"),
                                                   dtMutex_p);
              def->insertBefore(new DefExpr(refcMutex));
              
              def->insertBefore(
                new CallExpr(PRIMITIVE_SET_HEAPVAR, refcMutex,
                  new CallExpr(PRIMITIVE_CHPL_ALLOC, dtMutex->symbol, 
                    new_StringSymbol("alloc begin heap"))));
              def->insertBefore(
                new CallExpr(PRIMITIVE_SET_HEAPVAR, refc,
                  new CallExpr(PRIMITIVE_CHPL_ALLOC, dtInt[INT_SIZE_32]->symbol, 
                    new_StringSymbol("alloc begin heap"))));
              def->insertBefore(
                new CallExpr(PRIMITIVE_SET_HEAPVAR, var,
                  new CallExpr(PRIMITIVE_CHPL_ALLOC, getValueType(var->type)->symbol, 
                    new_StringSymbol("alloc begin heap"))));
              refcMap.put(var, refc);
              refcMutexMap.put(var, refcMutex);
            }

            Symbol* refc = refcMap.get(var);
            Symbol* refcMutex = refcMutexMap.get(var);

            call->insertAtTail(refc);
            call->insertAtTail(refcMutex);
            ArgSymbol *rc_arg = new ArgSymbol( INTENT_BLANK, 
                                               refc->name, 
                                               dtInt[INT_SIZE_32]->refType);
            ArgSymbol *rcm_arg = new ArgSymbol(INTENT_BLANK, 
                                               refcMutex->name, 
                                               dtMutex_p);
            FnSymbol  *fn = call->isResolved();
            fn->insertFormalAtTail(new DefExpr( rc_arg));
            fn->insertFormalAtTail(new DefExpr( rcm_arg));
            
            def->insertAfter( new CallExpr( PRIMITIVE_REFC_TOUCH, 
                                            var,
                                            refc,
                                            refcMutex));
            def->insertAfter( new CallExpr( PRIMITIVE_REFC_INIT, 
                                            var,
                                            refc,
                                            refcMutex));
            BlockStmt *mainfb = toBlockStmt(def->parentExpr);
            Expr      *laststmt = mainfb->body.last();
            CallExpr* ret = toCallExpr(laststmt);
            if (ret && ret->isPrimitive(PRIMITIVE_RETURN)) {
              laststmt->insertBefore( new CallExpr( PRIMITIVE_REFC_RELEASE, 
                                                    var,
                                                    refc,
                                                    refcMutex));
            } else {
              laststmt->insertAfter( new CallExpr( PRIMITIVE_REFC_RELEASE, 
                                                   var,
                                                   refc,
                                                   refcMutex));
            }
            
            // add touch + release for the begin block
            block->insertBefore( new CallExpr( PRIMITIVE_REFC_TOUCH, 
                                               var,
                                               refc,
                                               refcMutex));
            fn->insertBeforeReturn(new CallExpr(PRIMITIVE_REFC_RELEASE,
                                                actual_to_formal(use),
                                                rc_arg,
                                                rcm_arg));
          }
        }
      }
    }
  }
}


// Package args into a class and call a wrapper function with that
// object. The wrapper function will then call the function
// created by the previous parallel pass. This is a way to pass along
// multiple args through the limitation of one arg in the runtime's
// thread creation interface. 
static void
bundleArgs(BlockStmt* b) {
  ModuleSymbol* mod = b->getModule();
  BlockStmt *newb = new BlockStmt();
  newb->blockTag = b->blockTag;
  for_alist(expr, b->body) {
    CallExpr* fcall = toCallExpr(expr);
    if (!fcall || !fcall->isResolved()) {
      b->insertBefore(expr->remove());
      continue;
    } else {
      
      // create a new class to capture refs to locals
      const char* fname = (toSymExpr(fcall->baseExpr))->var->name;
      ClassType* ctype = new ClassType( CLASS_CLASS);
      TypeSymbol* new_c = new TypeSymbol( astr("_class_locals", 
                                               fname),
                                          ctype);

      // add the function args as fields in the class
      int i = 1;
      for_actuals(arg, fcall) {
        SymExpr *s = toSymExpr(arg);
        Symbol  *var = s->var; // arg or var
        var->isConcurrent = true;
        VarSymbol* field = new VarSymbol(astr("_", istr(i), "_", var->name), var->type);
        ctype->fields.insertAtTail(new DefExpr(field));
        i++;
      }
      mod->block->insertAtHead(new DefExpr(new_c));
        
      // create the class variable instance and allocate it
      VarSymbol *tempc = new VarSymbol( astr( "_args_for", 
                                              fname),
                                        ctype);
      b->insertBefore( new DefExpr( tempc));
      CallExpr *tempc_alloc = new CallExpr( PRIMITIVE_CHPL_ALLOC,
                                            ctype->symbol,
                                            new_StringSymbol( astr( "instance of class ", ctype->symbol->name)));
      b->insertBefore( new CallExpr( PRIMITIVE_MOVE,
                                     tempc,
                                     tempc_alloc));
      
      // set the references in the class instance
      i = 1;
      for_actuals(arg, fcall) {
        SymExpr *s = toSymExpr(arg);
        Symbol  *var = s->var; // var or arg
        CallExpr *setc=new CallExpr(PRIMITIVE_SET_MEMBER,
                                    tempc,
                                    ctype->getField(i),
                                    var);
        b->insertBefore( setc);
        i++;
      }
      
      // create wrapper-function that uses the class instance
      FnSymbol *wrap_fn = new FnSymbol( astr("wrap", fname));
      DefExpr  *fcall_def= (toSymExpr( fcall->baseExpr))->var->defPoint;
      ArgSymbol *wrap_c = new ArgSymbol( INTENT_BLANK, "c", ctype);
      wrap_fn->insertFormalAtTail( wrap_c);
      mod->block->insertAtTail(new DefExpr(wrap_fn));
      newb->insertAtTail( new CallExpr( wrap_fn, tempc));
      wrap_fn->insertAtTail(new CallExpr(PRIMITIVE_CHPL_FREE, wrap_c));
      
      // translate the original cobegin function
      CallExpr *new_cofn = new CallExpr( (toSymExpr(fcall->baseExpr))->var);
      for_fields(field, ctype) {  // insert args
        new_cofn->insertAtTail( new CallExpr(PRIMITIVE_GET_MEMBER_VALUE,
                                             wrap_c,
                                             field));
      }
      
      wrap_fn->retType = dtVoid;
      fcall->remove();                     // rm orig. call
      wrap_fn->insertAtHead(new_cofn);     // add new call
      wrap_fn->insertAtHead(new CallExpr(PRIMITIVE_THREAD_INIT));
      fcall_def->remove();                 // move orig. def
      mod->block->insertAtTail(fcall_def); // to top-level
      normalize(wrap_fn);
    }
  }
  b->replace(newb);
}


void
parallel(void) {
  compute_sym_uses();

  Vec<BlockStmt*> blocks;
  forv_Vec(BaseAST, ast, gAsts) {
    if (BlockStmt* block = toBlockStmt(ast))
      blocks.add(block);
  }

  forv_Vec(BlockStmt, block, blocks) {
    if (block->blockTag == BLOCK_BEGIN)
      heapAllocateLocals(block);
  }

  forv_Vec(BlockStmt, block, blocks) {
    if (block->blockTag == BLOCK_BEGIN || block->blockTag == BLOCK_COBEGIN)
      bundleArgs(block);
  }
}


void
insertWideReferences(void) {
  if (fLocal)
    return;

  Map<Type*,ClassType*> wideMap;

  forv_Vec(BaseAST, ast, gAsts) {
    if (BlockStmt* block = toBlockStmt(ast)) {
      if (block->blockTag == BLOCK_ON) {
        CallExpr* call = toCallExpr(block->body.tail);
        for_alist(expr, block->body) {
          if (expr != call)
            block->insertBefore(expr->remove());
        }

        ClassType* ct = new ClassType(CLASS_CLASS);
        TypeSymbol* cts = new TypeSymbol("_on_arg_class", ct);
        VarSymbol* locale = new VarSymbol("_tmp", dtInt[INT_SIZE_32]);
        block->insertBefore(new DefExpr(locale));
        block->insertBefore(new CallExpr(PRIMITIVE_MOVE, locale, new CallExpr(PRIMITIVE_LOCALE_ID)));
        block->getModule()->block->insertAtHead(new DefExpr(cts));
        VarSymbol* ci = new VarSymbol("_on_args", ct);
        block->insertBefore(new DefExpr(ci));
        block->insertBefore(new CallExpr(PRIMITIVE_MOVE, ci, new CallExpr(PRIMITIVE_CHPL_ALLOC, cts, new_StringSymbol("instance of on args class"))));
        int i = 1;
        FnSymbol* fn = call->isResolved();
        ArgSymbol* arg = new ArgSymbol(INTENT_BLANK, "_on_args", ct);
        compute_sym_uses(fn);
        for_formals_actuals(formal, actual, call) {
          if (call->argList.head == actual)
            continue; // skip first argument; contains locale
          ClassType* wide = wideMap.get(actual->typeInfo());
          if (!wide) {
            Type* base = actual->typeInfo();
            wide = new ClassType(CLASS_RECORD);
            TypeSymbol* ts =
              new TypeSymbol(astr("_wide_", base->symbol->cname), wide);
            theProgram->block->insertAtTail(new DefExpr(ts));
            wide->fields.insertAtTail(new DefExpr(new VarSymbol("locale", dtInt[INT_SIZE_32])));
            wide->fields.insertAtTail(new DefExpr(new VarSymbol("addr", base)));
          }
          VarSymbol* tmp = new VarSymbol("_tmp", wide);
          block->insertBefore(new DefExpr(tmp));
          block->insertBefore(new CallExpr(PRIMITIVE_SET_MEMBER, tmp, wide->getField(1), locale));
          block->insertBefore(new CallExpr(PRIMITIVE_SET_MEMBER, tmp, wide->getField(2), actual->remove()));
          VarSymbol* field = new VarSymbol(astr("_f", istr(i)), wide);
          ct->fields.insertAtTail(new DefExpr(field));
          i++;
          block->insertBefore(new CallExpr(PRIMITIVE_SET_MEMBER, ci, field, tmp));
          Vec<SymExpr*> usedefs;
          Vec<SymExpr*> defset;
          usedefs.copy(formal->uses);
          forv_Vec(SymExpr, def, formal->defs) {
            usedefs.add_exclusive(def);
            defset.set_add(def);
          }
          forv_Vec(SymExpr, se, usedefs) {
            if (CallExpr* call = toCallExpr(se->parentExpr)) {
              if (call->isPrimitive(PRIMITIVE_GET_REF)) {
                CallExpr* move = toCallExpr(call->parentExpr);
                if (!move || !move->isPrimitive(PRIMITIVE_MOVE))
                  INT_FATAL(call, "unexpected case");
                VarSymbol* tmp = new VarSymbol("_tmp", wide);
                move->insertBefore(new DefExpr(tmp));
                move->insertBefore(new CallExpr(PRIMITIVE_MOVE, tmp, new CallExpr(PRIMITIVE_GET_MEMBER_VALUE, arg, field)));
                move->replace(new CallExpr(PRIMITIVE_COMM_GET, move->get(1)->remove(), tmp));
                continue;
              } else if (call->isPrimitive(PRIMITIVE_GET_MEMBER_VALUE)) {
                CallExpr* move = toCallExpr(call->parentExpr);
                if (!move || !move->isPrimitive(PRIMITIVE_MOVE))
                  INT_FATAL(call, "unexpected case");
                VarSymbol* tmp = new VarSymbol("_tmp", wide);
                move->insertBefore(new DefExpr(tmp));
                move->insertBefore(new CallExpr(PRIMITIVE_MOVE, tmp, new CallExpr(PRIMITIVE_GET_MEMBER_VALUE, arg, field)));
                move->replace(new CallExpr(PRIMITIVE_COMM_GET_OFF, move->get(1)->remove(), tmp, getValueType(se->var->type)->symbol, call->get(2)->remove()));
                continue;
              } else if (call->isPrimitive(PRIMITIVE_SET_MEMBER) &&
                         call->get(1) == se) {
                VarSymbol* tmp = new VarSymbol("_tmp", wide);
                call->insertBefore(new DefExpr(tmp));
                call->insertBefore(new CallExpr(PRIMITIVE_MOVE, tmp, new CallExpr(PRIMITIVE_GET_MEMBER_VALUE, arg, field)));
                call->replace(new CallExpr(PRIMITIVE_COMM_PUT_OFF, tmp, call->get(3)->remove(), getValueType(se->var->type)->symbol, call->get(2)->remove()));
                continue;
              } else if (call->isPrimitive(PRIMITIVE_MOVE) &&
                         call->get(1) == se) {
                VarSymbol* rhs = new VarSymbol("_tmp", getValueType(se->var->type));
                call->insertBefore(new DefExpr(rhs));
                call->insertBefore(new CallExpr(PRIMITIVE_MOVE, rhs, call->get(2)->remove()));
                VarSymbol* tmp = new VarSymbol("_tmp", wide);
                call->insertBefore(new DefExpr(tmp));
                call->insertBefore(new CallExpr(PRIMITIVE_MOVE, tmp, new CallExpr(PRIMITIVE_GET_MEMBER_VALUE, arg, field)));
                call->replace(new CallExpr(PRIMITIVE_COMM_PUT, tmp, rhs));
                continue;
              }
            }
            USR_FATAL(se, "support for on-statements is preliminary");
          }
          formal->defPoint->remove();
        }
        call->insertAtTail(ci);
        fn->formals.insertAtTail(new DefExpr(arg));
        block->insertAfter(new CallExpr(PRIMITIVE_CHPL_FREE, ci));
      }
    }
  }
}

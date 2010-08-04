

#include "Absloc.h"
#include "AbslocInterface.h"

// Pile of InstructionAPI includes
#include "Expression.h"
#include "Register.h"
#include "Result.h"
#include "Dereference.h"

#include "dataflowAPI/h/stackanalysis.h"

#include "parseAPI/h/CFG.h"

using namespace Dyninst;
using namespace Dyninst::InstructionAPI;

void AbsRegionConverter::convertAll(InstructionAPI::Expression::Ptr expr,
				    Address addr,
				    ParseAPI::Function *func,
				    std::vector<AbsRegion> &regions) {
  // If we're a memory dereference, then convert us and all
  // used registers.
  if (dyn_detail::boost::dynamic_pointer_cast<Dereference>(expr)) {
    std::vector<InstructionAST::Ptr> tmp;
    // Strip dereference...
    expr->getChildren(tmp);
    for (std::vector<InstructionAST::Ptr>::const_iterator i = tmp.begin();
	 i != tmp.end(); ++i) {
      regions.push_back(convert(dyn_detail::boost::dynamic_pointer_cast<Expression>(*i), addr, func));
    }
  }
  
  // Otherwise just convert registers
  
  std::set<InstructionAST::Ptr> used;
  expr->getUses(used);
  for (std::set<InstructionAST::Ptr>::const_iterator j = used.begin();
       j != used.end(); ++j) {
    regions.push_back(convert(dyn_detail::boost::dynamic_pointer_cast<RegisterAST>(*j)));
  }
}

void AbsRegionConverter::convertAll(InstructionAPI::Instruction::Ptr insn,
				    Address addr,
				    ParseAPI::Function *func,
				    std::vector<AbsRegion> &used,
				    std::vector<AbsRegion> &defined) {
  if (!usedCache(addr, func, used)) {
    std::set<RegisterAST::Ptr> regsRead;
    insn->getReadSet(regsRead);
    
    for (std::set<RegisterAST::Ptr>::const_iterator i = regsRead.begin();
	 i != regsRead.end(); ++i) {
      used.push_back(AbsRegionConverter::convert(*i));
    }
    
    if (insn->readsMemory()) {
      std::set<Expression::Ptr> memReads;
      insn->getMemoryReadOperands(memReads);
      for (std::set<Expression::Ptr>::const_iterator r = memReads.begin();
	   r != memReads.end();
	   ++r) {
	used.push_back(AbsRegionConverter::convert(*r, addr, func));
      }
    }
  }
  if (!definedCache(addr, func, defined)) {
    // Defined time
    std::set<RegisterAST::Ptr> regsWritten;
    insn->getWriteSet(regsWritten);
    
    for (std::set<RegisterAST::Ptr>::const_iterator i = regsWritten.begin();
	 i != regsWritten.end(); ++i) {
      defined.push_back(AbsRegionConverter::convert(*i));
    }
    
    if (insn->writesMemory()) {
      std::set<Expression::Ptr> memWrites;
      insn->getMemoryWriteOperands(memWrites);
      for (std::set<Expression::Ptr>::const_iterator r = memWrites.begin();
	   r != memWrites.end();
	   ++r) {
	defined.push_back(AbsRegionConverter::convert(*r, addr, func));
      }
    }
  }

  if (cacheEnabled_) {
    used_cache_[func][addr] = used;
    defined_cache_[func][addr] = defined;
  }
}

AbsRegion AbsRegionConverter::convert(RegisterAST::Ptr reg) {
  // FIXME:
  // Upcast register so we can be sure to match things later
  AbsRegion tmp = AbsRegion(Absloc(reg->getID().getBaseRegister()));

  //std::cerr << "ARC::convert from " << reg->format() << " to "
  //    << tmp.format() << std::endl;
  return tmp;
}

AbsRegion AbsRegionConverter::convert(Expression::Ptr exp,
				      Address addr,
				      ParseAPI::Function *func) {
    // We want to simplify the expression as much as possible given 
    // currently known state, and then quantify it as one of the following:
    // 
    // Stack: a memory access based off the current frame pointer (FP) or
    //   stack pointer (SP). If we can determine an offset from the "top"
    //   of the stack we create a stack slot location. Otherwise we create
    //   a "stack" location that represents all stack locations.
    //
    // Heap: a memory access to a generic pointer.
    //
    // Memory: a memory access to a known address. 
    //
    // TODO: aliasing relations. Aliasing SUCKS. 

    // Since we have an Expression as input, we don't have the dereference
    // operator.

    // Here's the logic:
    // If no registers are used:
    //   If only immediates are used:
    //     Evaluate and create a MemLoc.
    //   If a dereference exists:
    //     WTF???
    // If registers are used:
    //   If the only register is the FP AND the function has a stack frame:
    //     Set FP to 0, eval, and create a specific StackLoc.
    //   If the only register is the SP:
    //     If we know the contents of SP:
    //       Eval and create a specific StackLoc
    //     Else create a generic StackLoc.
    //   If a non-stack register is used:
    //     Create a generic MemLoc.

    long spHeight = 0;
    int spRegion = 0;
    bool stackDefined = getCurrentStackHeight(func,
					     addr, 
					     spHeight, 
					     spRegion);
    long fpHeight = 0;
    int fpRegion = 0;
    bool frameDefined = getCurrentFrameHeight(func,
					     addr,
					     fpHeight,
					     fpRegion);

    bool isStack = false;
    bool isFrame = false;

    static Expression::Ptr theStackPtr(new RegisterAST(MachRegister::getStackPointer(Arch_x86)));
    static Expression::Ptr theStackPtr64(new RegisterAST(MachRegister::getStackPointer(Arch_x86_64)));
    
    static Expression::Ptr theFramePtr(new RegisterAST(MachRegister::getFramePointer(Arch_x86)));
    static Expression::Ptr theFramePtr64(new RegisterAST(MachRegister::getFramePointer(Arch_x86_64)));

    static Expression::Ptr thePC(new RegisterAST(MachRegister::getPC(Arch_x86)));
    static Expression::Ptr thePC64(new RegisterAST(MachRegister::getPC(Arch_x86_64)));
    
    // We currently have to try and bind _every_ _single_ _alias_
    // of the stack pointer...
    if (exp->bind(theStackPtr.get(), Result(s32, spHeight)) ||
	exp->bind(theStackPtr64.get(), Result(s64, spHeight))) {
      isStack = true;
    }
    if (exp->bind(theFramePtr.get(), Result(s32, fpHeight)) ||
	exp->bind(theFramePtr64.get(), Result(s64, fpHeight))) {
      isFrame = true;
    }
    
    // Bind the IP, why not...
    exp->bind(thePC.get(), Result(u32, addr));
    exp->bind(thePC64.get(), Result(u64, addr));

    Result res = exp->eval();

    if (isFrame) {
      if (res.defined && frameDefined) {
	return AbsRegion(Absloc(res.convert<Address>(),
				fpRegion,
				func->name()));
      }
      else {
	return AbsRegion(Absloc::Stack);
      }
    }

    if (isStack) {
      if (res.defined && stackDefined) {
	return AbsRegion(Absloc(res.convert<Address>(),
				spRegion,
				func->name()));
      }
      else {
	return AbsRegion(Absloc::Stack);
      }
    }

    // Otherwise we're on the heap
    if (res.defined) {
      return AbsRegion(Absloc(res.convert<Address>()));
    }
    else {
      return AbsRegion(Absloc::Heap);
    }
}

AbsRegion AbsRegionConverter::stack(Address addr,
				    ParseAPI::Function *func,
				    bool push) {
    long spHeight = 0;
    int spRegion = 0;
    bool stackExists = getCurrentStackHeight(func,
					     addr, 
					     spHeight, 
					     spRegion);
    if (!stackExists) {
      return AbsRegion(Absloc::Stack);
    }

    if (push) {
      int word_size = func->isrc()->getAddressWidth();
      spHeight -= word_size;
    }

    return AbsRegion(Absloc(spHeight,
			    spRegion,
			    func->name()));
}

AbsRegion AbsRegionConverter::frame(Address addr,
				    ParseAPI::Function *func,
				    bool push) {
    long fpHeight = 0;
    int fpRegion = 0;
    bool frameExists = getCurrentFrameHeight(func,
					     addr, 
					     fpHeight, 
					     fpRegion);
    if (!frameExists) {
      return AbsRegion(Absloc::Heap);
    }

    if (push) {
      int word_size = func->isrc()->getAddressWidth();
      fpHeight -= word_size;
    }
    
    return AbsRegion(Absloc(fpHeight,
			    fpRegion,
			    func->name()));
}

bool AbsRegionConverter::getCurrentStackHeight(ParseAPI::Function *func,
					       Address addr,
					       long &height,
					       int &region) {
  StackAnalysis sA(func);

  StackAnalysis::Height heightSA = sA.findSP(addr);

  // Ensure that analysis has been performed.
  assert(!heightSA.isTop());
  
  if (heightSA.isBottom()) {
    return false;
  }
  
  height = heightSA.height();
  region = heightSA.region()->name();
  
  return true;
}

bool AbsRegionConverter::getCurrentFrameHeight(ParseAPI::Function *func,
					       Address addr,
					       long &height,
					       int &region) {
  StackAnalysis sA(func);

  StackAnalysis::Height heightSA = sA.findFP(addr);

  // Ensure that analysis has been performed.
  assert(!heightSA.isTop());
  
  if (heightSA.isBottom()) {
    return false;
  }
  
  height = heightSA.height();
  region = heightSA.region()->name();
  
  return true;
}


bool AbsRegionConverter::usedCache(Address addr,
				   ParseAPI::Function *func,
				   std::vector<AbsRegion> &used) {
  if (!cacheEnabled_) return false;
  FuncCache::iterator iter = used_cache_.find(func);
  if (iter == used_cache_.end()) return false;
  AddrCache::iterator iter2 = iter->second.find(addr);
  if (iter2 == iter->second.end()) return false;
  used = iter2->second;
  return true;
}

bool AbsRegionConverter::definedCache(Address addr,
				      ParseAPI::Function *func,
				      std::vector<AbsRegion> &defined) {
  if (!cacheEnabled_) return false;
  FuncCache::iterator iter = defined_cache_.find(func);
  if (iter == defined_cache_.end()) return false;
  AddrCache::iterator iter2 = iter->second.find(addr);
  if (iter2 == iter->second.end()) return false;
  defined = iter2->second;
  return true;
}

///////////////////////////////////////////////////////
// Create a set of Assignments from an InstructionAPI
// Instruction.
///////////////////////////////////////////////////////

void AssignmentConverter::convert(const Instruction::Ptr I, 
                                  const Address &addr,
				  ParseAPI::Function *func,
				  std::vector<Assignment::Ptr> &assignments) {
  if (cache(func, addr, assignments)) return;

  // Decompose the instruction into a set of abstract assignments.
  // We don't have the Definition class concept yet, so we'll do the 
  // hard work here. 
  // Two phases:
  // 1) Special-cased for IA32 multiple definition instructions,
  //    based on the opcode of the instruction
  // 2) Generic handling for things like flags and the PC. 

  // Non-PC handling section
  switch(I->getOperation().getID()) {
  case e_push: {
    // SP = SP - 4 
    // *SP = <register>
 
    std::vector<Operand> operands;
    I->getOperands(operands);

    // According to the InstructionAPI, the first operand will be the argument, the second will be ESP.
    assert(operands.size() == 2);

    // The argument can be any of the following:
    // 1) a register (push eax);
    // 2) an immediate value (push $deadbeef)
    // 3) a memory location. 

    std::vector<AbsRegion> oper0;
    aConverter.convertAll(operands[0].getValue(),
				   addr,
				   func,
				   oper0);

    handlePushEquivalent(I, addr, func, oper0, assignments);
    break;
  }
  case e_call: {
    // This can be seen as a push of the PC...

    std::vector<AbsRegion> pcRegion;
    pcRegion.push_back(Absloc::makePC(func->isrc()->getArch()));
    
    handlePushEquivalent(I, addr, func, pcRegion, assignments);

    // Now for the PC definition
    // Assume full intra-dependence of non-flag and non-pc registers. 
    std::vector<AbsRegion> used;
    std::vector<AbsRegion> defined;

    aConverter.convertAll(I,
			  addr,
			  func,
			  used,
			  defined);

    Assignment::Ptr a = Assignment::Ptr(new Assignment(I, addr, func, pcRegion[0]));
    if (!used.empty()) {
      // Indirect call
      a->addInputs(used);
    }
    else {
      a->addInputs(pcRegion);
    }
    assignments.push_back(a);
    break;
  }
  case e_pop: {
    // <reg> = *SP
    // SP = SP + 4/8
    // Amusingly... this doesn't have an intra-instruction dependence. It should to enforce
    // the order that <reg> = *SP happens before SP = SP - 4, but since the input to both 
    // uses of SP in this case are the, well, input values... no "sideways" edges. 
    // However, we still special-case it so that SP doesn't depend on the incoming stack value...
    // Also, we use the same logic for return, defining it as
    // PC = *SP
    // SP = SP + 4/8

    // As with push, eSP shows up as operand 1. 

    std::vector<Operand> operands;
    I->getOperands(operands);

    // According to the InstructionAPI, the first operand will be the explicit register, the second will be ESP.
    assert(operands.size() == 2);

    std::vector<AbsRegion> oper0;
    aConverter.convertAll(operands[0].getValue(),
				   addr,
				   func,
				   oper0);

    handlePopEquivalent(I, addr, func, oper0, assignments);
    break;
  }
  case e_leave: {
    // a leave is equivalent to:
    // mov ebp, esp
    // pop ebp
    // From a definition POV, we have the following:
    // SP = BP
    // BP = *SP
        
    // BP    STACK[newSP]
    //  |    |
    //  v    v
    // SP -> BP
        
    // This is going to give the stack analysis fits... for now, I think it just reverts the
    // stack depth to 0. 

    // TODO FIXME update stack analysis to make this really work. 
        
    AbsRegion sp(Absloc::makeSP(func->isrc()->getArch()));
    AbsRegion fp(Absloc::makeFP(func->isrc()->getArch()));

    // Should be "we assign SP using FP"
    Assignment::Ptr spA = Assignment::Ptr(new Assignment(I,
							 addr,
							 func,
							 sp));
    spA->addInput(fp);

    // And now we want "FP = (stack slot -2*wordsize)"
    /*
      AbsRegion stackTop(Absloc(0,
      0,
      func->name()));
    */
    // Actually, I think this is ebp = pop esp === ebp = pop ebp
    Assignment::Ptr fpA = Assignment::Ptr(new Assignment(I,
							 addr,
							 func,
							 fp));
    //fpA->addInput(aConverter.stack(addr + I->size(), func, false));
    fpA->addInput(aConverter.frame(addr, func, false));

    assignments.push_back(spA);
    assignments.push_back(fpA);
    break;
  }
  case e_ret_near:
  case e_ret_far: {
    // PC = *SP
    // SP = SP + 4/8
    // Like pop, except it's all implicit.

    AbsRegion pc = AbsRegion(Absloc::makePC(func->isrc()->getArch()));
    Assignment::Ptr pcA = Assignment::Ptr(new Assignment(I, 
							 addr,
							 func,
							 pc));
    pcA->addInput(aConverter.stack(addr, func, false));

    AbsRegion sp = AbsRegion(Absloc::makeSP(func->isrc()->getArch()));
    Assignment::Ptr spA = Assignment::Ptr(new Assignment(I,
							 addr,
							 func,
							 sp));
    spA->addInput(sp);

    assignments.push_back(pcA);
    assignments.push_back(spA);
    break;
  }

  case e_xchg: {
    // xchg defines two abslocs, and uses them as appropriate...

    std::vector<Operand> operands;
    I->getOperands(operands);

    // According to the InstructionAPI, the first operand will be the argument, the second will be ESP.
    assert(operands.size() == 2);

    // We use the first to define the second, and vice versa
    std::vector<AbsRegion> oper0;
    aConverter.convertAll(operands[0].getValue(),
				   addr,
				   func,
				   oper0);
    
    std::vector<AbsRegion> oper1;
    aConverter.convertAll(operands[1].getValue(),
				   addr,
				   func,
				   oper1);

    // Okay. We may have a memory reference in here, which will
    // cause either oper0 or oper1 to have multiple entries (the
    // remainder will be registers). So. Use everything from oper1
    // to define oper0[0], and vice versa.
    
    Assignment::Ptr a = Assignment::Ptr(new Assignment(I, addr, func, oper0[0]));
    a->addInputs(oper1);

    Assignment::Ptr b = Assignment::Ptr(new Assignment(I, addr, func, oper1[0]));
    b->addInputs(oper0);

    assignments.push_back(a);
    assignments.push_back(b);
    break;
  }
        
  default:
    // Assume full intra-dependence of non-flag and non-pc registers. 
    std::vector<AbsRegion> used;
    std::vector<AbsRegion> defined;

    aConverter.convertAll(I,
			  addr,
			  func,
			  used,
			  defined);
    
    for (std::vector<AbsRegion>::const_iterator i = defined.begin();
	 i != defined.end(); ++i) {
      Assignment::Ptr a = Assignment::Ptr(new Assignment(I, addr, func, *i));
      a->addInputs(used);
      assignments.push_back(a);
    }
    break;
  }
    

  // Now for flags...
  // According to Matt, the easiest way to represent dependencies for flags on 
  // IA-32/AMD-64 is to have them depend on the inputs to the instruction and 
  // not the outputs of the instruction; therefore, there's no intra-instruction
  // dependence. 

  // PC-handling section
  // Most instructions use the PC to set the PC. This includes calls, relative branches,
  // and the like. So we're basically looking for indirect branches or absolute branches.
  // (are there absolutes on IA-32?).
  // Also, conditional branches and the flag registers they use. 

  if (cacheEnabled_) {
    cache_[func][addr] = assignments;
  }

}

void AssignmentConverter::handlePushEquivalent(const Instruction::Ptr I,
					       Address addr,
					       ParseAPI::Function *func,
					       std::vector<AbsRegion> &operands,
					       std::vector<Assignment::Ptr> &assignments) {
  // The handled-in operands are used to define *SP
  // And then we update SP
  
  AbsRegion stackTop = aConverter.stack(addr, func, true);
  AbsRegion sp(Absloc::makeSP(func->isrc()->getArch()));

  Assignment::Ptr spA = Assignment::Ptr(new Assignment(I,
						       addr,
						       func,
						       stackTop));
  spA->addInputs(operands);
  spA->addInput(sp);

  Assignment::Ptr spB = Assignment::Ptr(new Assignment(I, addr, func, sp));
  spB->addInput(sp);

  assignments.push_back(spA);
  assignments.push_back(spB);
}

void AssignmentConverter::handlePopEquivalent(const Instruction::Ptr I,
					      Address addr,
					      ParseAPI::Function *func,
					      std::vector<AbsRegion> &operands,
					      std::vector<Assignment::Ptr> &assignments) {
  // We use the top of the stack and any operands beyond the first.
  // (Can you pop into memory?)

  AbsRegion stackTop = aConverter.stack(addr, func, false);
  AbsRegion sp(Absloc::makeSP(func->isrc()->getArch()));
  
  Assignment::Ptr spA = Assignment::Ptr(new Assignment(I,
						       addr,
						       func,
						       operands[0]));
  spA->addInput(stackTop);
  spA->addInput(sp);

  for (unsigned i = 1; i < operands.size(); i++) {
    spA->addInput(operands[i]);
  }

  // Now stack assignment
  Assignment::Ptr spB = Assignment::Ptr(new Assignment(I, addr, func, sp));
  spB->addInput(sp);

  assignments.push_back(spA);
  assignments.push_back(spB);
}

bool AssignmentConverter::cache(ParseAPI::Function *func, 
				Address addr, 
				std::vector<Assignment::Ptr> &assignments) {
  if (!cacheEnabled_) {
    return false;
  }
  FuncCache::iterator iter = cache_.find(func);
  if (iter == cache_.end()) {
    return false;
  }
  AddrCache::iterator iter2 = iter->second.find(addr);
  if (iter2 == iter->second.end()) {
    return false;
  }
  assignments = iter2->second;
  return true;
}



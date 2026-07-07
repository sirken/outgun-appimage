/*
 *  function_utility.h
 *
 *  Copyright (C) 2004, 2008, 2010 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef FUNCTION_UTILITY_INC
#define FUNCTION_UTILITY_INC

// function_utility.h : template classes to wrap different kinds of function objects

// Function# provides a common interface for storing and accessing function references
// FunctionHolder# manages a pointer to Function and behaves just like a regular function pointer would
// Hook# is a FunctionHolder#-like object that provides a different (more explicit) call syntax
// Hookable# is a base class to allow easy inclusion of a Hook# in a class (use with care)

// Fun# (deriving from Function#) is a helper giving a regular function pointer the interface of Function#
// MemFun# (deriving from Function#) is a helper allowing function objects bound to a member function of an object

// generating new versions with different amounts of arguments should be easy when needed; shame creating them can't be circumvented


// 0-argument Function#, Hook#, Hookable# :

template<class RetT>
class Function0 {
public:
    virtual ~Function0() throw () { }
    virtual RetT operator()() const throw () = 0;
    virtual Function0* clone() const throw () = 0;
};

template<class RetT>
class FunctionHolder0 {
public:
    typedef FunctionHolder0 ThisT;
    typedef Function0<RetT> FunctionT;

    FunctionHolder0(FunctionT* fn) throw () : hookFn(fn) { }
    FunctionHolder0(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~FunctionHolder0() throw () { delete hookFn; }
    void operator=(FunctionT* fn) throw () { delete hookFn; hookFn = fn; }
    void operator=(const ThisT& o) throw () { if (&o == this) return; delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()() const throw () { return (*hookFn)(); }

    operator const void*() const throw () { return hookFn ? this : 0; }
    bool operator!() const throw () { return !hookFn; }

private:
    FunctionT* hookFn;
};

template<class RetT>
class Hook0 {
public:
    typedef Function0<RetT> FunctionT;

    Hook0() throw () : holder(0) { }
    void set(FunctionT* fn) throw () { holder = fn; }    // the ownership is transferred
    bool active() const throw () { return holder; }
    RetT call() const throw () { return holder ? holder() : RetT(); }

private:
    FunctionHolder0<RetT> holder;
};

template<class RetT>
class Hookable0 {
public:
    typedef typename Hook0<RetT>::FunctionT HookFunctionT;

    virtual ~Hookable0() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook() const throw () { return hook.call(); }

private:
    Hook0<RetT> hook;
};

// 1-argument Function#, Hook#, Hookable# :

template<class RetT, class Arg1T>
class Function1 {
public:
    virtual ~Function1() throw () { }
    virtual RetT operator()(Arg1T) const throw () = 0;
    virtual Function1* clone() const throw () = 0;
};

template<class RetT, class Arg1T>
class FunctionHolder1 {
public:
    typedef FunctionHolder1 ThisT;
    typedef Function1<RetT, Arg1T> FunctionT;

    FunctionHolder1(FunctionT* fn) throw () : hookFn(fn) { }
    FunctionHolder1(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~FunctionHolder1() throw () { delete hookFn; }
    void operator=(FunctionT* fn) throw () { delete hookFn; hookFn = fn; }
    void operator=(const ThisT& o) throw () { if (&o == this) return; delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()(Arg1T a1) const throw () { return (*hookFn)(a1); }

    operator const void*() const throw () { return hookFn ? this : 0; }
    bool operator!() const throw () { return !hookFn; }

private:
    FunctionT* hookFn;
};

template<class RetT, class Arg1T>
class Hook1 {
public:
    typedef Function1<RetT, Arg1T> FunctionT;

    Hook1() throw () : holder(0) { }
    void set(FunctionT* fn) throw () { holder = fn; }    // the ownership is transferred
    bool active() const throw () { return holder; }
    RetT call(Arg1T a1) const throw () { return holder ? holder(a1) : RetT(); }

private:
    FunctionHolder1<RetT, Arg1T> holder;
};

template<class RetT, class Arg1T>
class Hookable1 {
public:
    typedef typename Hook1<RetT, Arg1T>::FunctionT HookFunctionT;

    virtual ~Hookable1() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook(Arg1T a1) const throw () { return hook.call(a1); }

private:
    Hook1<RetT, Arg1T> hook;
};

// 2-argument Function#, Hook#, Hookable# :

template<class RetT, class Arg1T, class Arg2T>
class Function2 {
public:
    virtual ~Function2() throw () { }
    virtual RetT operator()(Arg1T, Arg2T) const throw () = 0;
    virtual Function2* clone() const throw () = 0;
};

template<class RetT, class Arg1T, class Arg2T>
class FunctionHolder2 {
public:
    typedef FunctionHolder2 ThisT;
    typedef Function2<RetT, Arg1T, Arg2T> FunctionT;

    FunctionHolder2(FunctionT* fn) throw () : hookFn(fn) { }
    FunctionHolder2(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~FunctionHolder2() throw () { delete hookFn; }
    void operator=(FunctionT* fn) throw () { delete hookFn; hookFn = fn; }
    void operator=(const ThisT& o) throw () { if (&o == this) return; delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()(Arg1T a1, Arg2T a2) const throw () { return (*hookFn)(a1, a2); }

    operator const void*() const throw () { return hookFn ? this : 0; }
    bool operator!() const throw () { return !hookFn; }

private:
    FunctionT* hookFn;
};

template<class RetT, class Arg1T, class Arg2T>
class Hook2 {
public:
    typedef Function2<RetT, Arg1T, Arg2T> FunctionT;

    Hook2() throw () : holder(0) { }
    void set(FunctionT* fn) throw () { holder = fn; }    // the ownership is transferred
    bool active() const throw () { return holder; }
    RetT call(Arg1T a1, Arg2T a2) const throw () { return holder ? holder(a1, a2) : RetT(); }

private:
    FunctionHolder2<RetT, Arg1T, Arg2T> holder;
};

template<class RetT, class Arg1T, class Arg2T>
class Hookable2 {
public:
    typedef typename Hook2<RetT, Arg1T, Arg2T>::FunctionT HookFunctionT;

    virtual ~Hookable2() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook(Arg1T a1, Arg2T a2) const throw () { return hook.call(a1, a2); }

private:
    Hook2<RetT, Arg1T, Arg2T> hook;
};

// 3-argument Function#, Hook#, Hookable# :

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class Function3 {
public:
    virtual ~Function3() throw () { }
    virtual RetT operator()(Arg1T, Arg2T, Arg3T) const throw () = 0;
    virtual Function3* clone() const throw () = 0;
};

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class FunctionHolder3 {
public:
    typedef FunctionHolder3 ThisT;
    typedef Function3<RetT, Arg1T, Arg2T, Arg3T> FunctionT;

    FunctionHolder3(FunctionT* fn) throw () : hookFn(fn) { }
    FunctionHolder3(const ThisT& o) throw () : hookFn(o.hookFn ? o.hookFn->clone() : 0) { }
    ~FunctionHolder3() throw () { delete hookFn; }
    void operator=(FunctionT* fn) throw () { delete hookFn; hookFn = fn; }
    void operator=(const ThisT& o) throw () { if (&o == this) return; delete hookFn; hookFn = o.hookFn ? o.hookFn->clone() : 0; }

    RetT operator()(Arg1T a1, Arg2T a2, Arg3T a3) const throw () { return (*hookFn)(a1, a2, a3); }

    operator const void*() const throw () { return hookFn ? this : 0; }
    bool operator!() const throw () { return !hookFn; }

private:
    FunctionT* hookFn;
};

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class Hook3 {
public:
    typedef Function3<RetT, Arg1T, Arg2T, Arg3T> FunctionT;

    Hook3() throw () : holder(0) { }
    void set(FunctionT* fn) throw () { holder = fn; }    // the ownership is transferred
    bool active() const throw () { return holder; }
    RetT call(Arg1T a1, Arg2T a2, Arg3T a3) const throw () { return holder ? holder(a1, a2, a3) : RetT(); }

private:
    FunctionHolder3<RetT, Arg1T, Arg2T, Arg3T> holder;
};

template<class RetT, class Arg1T, class Arg2T, class Arg3T>
class Hookable3 {
public:
    typedef typename Hook3<RetT, Arg1T, Arg2T, Arg3T>::FunctionT HookFunctionT;

    virtual ~Hookable3() throw () { }
    void setHook(HookFunctionT* fn) throw () { hook.set(fn); }   // the ownership is transferred
    bool isHooked() const throw () { return hook.active(); }

protected:
    RetT callHook(Arg1T a1, Arg2T a2, Arg3T a3) const throw () { return hook.call(a1, a2, a3); }

private:
    Hook3<RetT, Arg1T, Arg2T, Arg3T> hook;
};

// 0-argument MemFun, ConstMemFun, Fun

template<class HostClass, class ReturnT>
class MemFun0 : public Function0<ReturnT> {
    HostClass* host;
    ReturnT (HostClass::*function)() throw ();

public:
    MemFun0(HostClass* h, ReturnT (HostClass::*memFun)() throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()() const throw () { return (host->*function)(); }
    MemFun0* clone() const throw () { return new MemFun0(host, function); }
};

template<class HostClass, class ReturnT>
class ConstMemFun0 : public Function0<ReturnT> {
    const HostClass* host;
    ReturnT (HostClass::*function)() const throw ();

public:
    ConstMemFun0(const HostClass* h, ReturnT (HostClass::*memFun)() const throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()() const throw () { return (host->*function)(); }
    ConstMemFun0* clone() const throw () { return new ConstMemFun0(host, function); }
};

template<class ReturnT>
class Fun0 : public Function0<ReturnT> {
    ReturnT (*function)() throw ();

public:
    Fun0(ReturnT (*fun)() throw ()) throw () : function(fun) { }
    ReturnT operator()() const throw () { return (*function)(); }
    Fun0* clone() const throw () { return new Fun0(function); }
};

template<class HostClass, class ReturnT>
MemFun0<HostClass, ReturnT>* newMemFun0(HostClass* h, ReturnT (HostClass::*memFun)() throw ()) throw () { return new MemFun0<HostClass, ReturnT>(h, memFun); }

template<class HostClass, class ReturnT>
ConstMemFun0<HostClass, ReturnT>* newConstMemFun0(const HostClass* h, ReturnT (HostClass::*memFun)() const throw ()) throw () { return new ConstMemFun0<HostClass, ReturnT>(h, memFun); }

template<class ReturnT>
Fun0<ReturnT>* newFun0(ReturnT (*function)() throw ()) throw () { return new Fun0<ReturnT>(function); }

// 1-argument MemFun, ConstMemFun, Fun

template<class HostClass, class ReturnT, class Arg1T>
class MemFun1 : public Function1<ReturnT, Arg1T> {
    HostClass* host;
    ReturnT (HostClass::*function)(Arg1T) throw ();

public:
    MemFun1(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()(Arg1T arg) const throw () { return (host->*function)(arg); }
    MemFun1* clone() const throw () { return new MemFun1(host, function); }
};

template<class HostClass, class ReturnT, class Arg1T>
class ConstMemFun1 : public Function1<ReturnT, Arg1T> {
    const HostClass* host;
    ReturnT (HostClass::*function)(Arg1T) const throw ();

public:
    ConstMemFun1(const HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) const throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()(Arg1T arg) const throw () { return (host->*function)(arg); }
    ConstMemFun1* clone() const throw () { return new ConstMemFun1(host, function); }
};

template<class ReturnT, class Arg1T>
class Fun1 : public Function1<ReturnT, Arg1T> {
    ReturnT (*function)(Arg1T) throw ();

public:
    Fun1(ReturnT (*fun)(Arg1T) throw ()) throw () : function(fun) { }
    ReturnT operator()(Arg1T arg) const throw () { return (*function)(arg); }
    Fun1* clone() const throw () { return new Fun1(function); }
};

template<class HostClass, class ReturnT, class Arg1T>
MemFun1<HostClass, ReturnT, Arg1T>* newMemFun1(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) throw ()) throw () { return new MemFun1<HostClass, ReturnT, Arg1T>(h, memFun); }

template<class HostClass, class ReturnT, class Arg1T>
ConstMemFun1<HostClass, ReturnT, Arg1T>* newConstMemFun1(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T) throw ()) throw () { return new ConstMemFun1<HostClass, ReturnT, Arg1T>(h, memFun); }

template<class ReturnT, class Arg1T>
Fun1<ReturnT, Arg1T>* newFun1(ReturnT (*function)(Arg1T) throw ()) throw () { return new Fun1<ReturnT, Arg1T>(function); }

// 2-argument MemFun, Fun

template<class HostClass, class ReturnT, class Arg1T, class Arg2T>
class MemFun2 : public Function2<ReturnT, Arg1T, Arg2T> {
    HostClass* host;
    ReturnT (HostClass::*function)(Arg1T, Arg2T) throw ();

public:
    MemFun2(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T, Arg2T) throw ()) throw () : host(h), function(memFun) { }
    ReturnT operator()(Arg1T arg1, Arg2T arg2) const throw () { return (host->*function)(arg1, arg2); }
};

template<class ReturnT, class Arg1T, class Arg2T>
class Fun2 : public Function2<ReturnT, Arg1T, Arg2T> {
    ReturnT (*function)(Arg1T, Arg2T) throw ();

public:
    Fun2(ReturnT (*fun)(Arg1T, Arg2T) throw ()) : function(fun) { }
    ReturnT operator()(Arg1T arg1, Arg2T arg2) const throw () { return (*function)(arg1, arg2); }
    Fun2* clone() const throw () { return new Fun2(function); }
};

template<class HostClass, class ReturnT, class Arg1T, class Arg2T>
Function2<ReturnT, Arg1T, Arg2T>* newMemFun2(HostClass* h, ReturnT (HostClass::*memFun)(Arg1T, Arg2T) throw ()) throw () { return new MemFun2<HostClass, ReturnT, Arg1T, Arg2T>(h, memFun); }

template<class ReturnT, class Arg1T, class Arg2T>
Fun2<ReturnT, Arg1T, Arg2T>* newFun2(ReturnT (*function)(Arg1T, Arg2T) throw ()) throw () { return new Fun2<ReturnT, Arg1T, Arg2T>(function); }

// 0-argument StripConstRef0

template<class RetT>
class StripConstRef0 : public Function0<RetT> {
    Function0<const RetT&>& base;

public:
    StripConstRef0(Function0<const RetT&>& base_) throw () : base(base_) { }
    RetT operator()() const throw () { return base(); }
    StripConstRef0* clone() const throw () { return new StripConstRef0(base); }
};

template<class RetT>
StripConstRef0<RetT>* newStripConstRef0(Function0<const RetT&>& base_) throw () { return new StripConstRef0<RetT>(base_); }

// simple utility classes that utilize hook functions

class AtScopeExit {
    FunctionHolder0<void> action;

public:
    AtScopeExit(Function0<void>* action_) throw () : action(action_) { }
    ~AtScopeExit() throw () { action(); }
};

#endif

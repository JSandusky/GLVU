#pragma once

//****************************************************************************
//
//  Class:      loose_ptr<T>
//
//  Purpose:    An invasive pointer that gets reset when the target object is destroyed
//              (polymorphism is not supported here, same problems as shared_from_this).
//
//              Basically it's an intrusive weak_ptr which is useful.
//
//****************************************************************************
template<class TYPE> 
class loose_ptr 
{
    // prevent "obj=0;" because it doesn't work right
    loose_ptr<TYPE>& operator=(int);
private:
    TYPE* ptr_;
    loose_ptr* next_;
public:
    /// default constructor
    loose_ptr() : ptr_(0), next_(0) { }
    /// construct from dumb pointer
    loose_ptr(TYPE* p) : ptr_(0), next_(0) { set(p); }
    /// construct from derived class dumb pointer
    template<class T> loose_ptr(T* p) : ptr_(0), next_(0) { set(p); }
    /// construct from smart_ptr, shared_ptr, etc.
    template<class T> loose_ptr(const T& t) : ptr_(0), next_(0) { set(t.get()); }
    /// copy constructor (specialization of above)
    loose_ptr(const loose_ptr& rhs) : ptr_(0), next_(0) { set(rhs.ptr_); }
    /// destructor
    ~loose_ptr() { set(0); }

    TYPE* ptr() const { return ptr_; }
    TYPE* get() const { return ptr_; }
    TYPE* operator->() const { assert(ptr_); return ptr_; }
    TYPE& operator*() const { assert(ptr_); return *ptr_; }

    bool operator==(TYPE* p) const { return p == ptr_; }
    bool operator!=(TYPE* p) const { return p != ptr_; }

    bool expired() const { return !ptr_; }

    class incomplete_class;
    operator const incomplete_class*() const { return (const incomplete_class*)ptr_; }

    /// Assign from dumb pointer
    loose_ptr& operator=(TYPE* p) { set(p); return *this; }
    
    template<class T> loose_ptr& operator=(const T& t) { set(t.get()); return *this; }
    
    loose_ptr& operator=(const loose_ptr& rhs) { set(rhs.ptr_); return *this; }

    void reset(TYPE* aPtr = 0) { set(aPtr); }
    void set(TYPE* aPtr)
    {
        if (aPtr != ptr_) 
        {
            const enable_loose_ptr<TYPE>* ptr = ptr_;
            if (ptr != 0) 
            {
                if (ptr->loose_ptr_head_ == this) 
                {
                    ptr->loose_ptr_head_ = next_;
                } else 
                {
                    loose_ptr* prev = 0;
                    for (loose_ptr* p = ptr->loose_ptr_head_; p && p != this; prev = p, p = p->next_);
                    assert(prev && prev->next_ == this);
                    if (prev && prev->next_ == this)
                        prev->next_ = next_;
                }
                next_ = 0;
            }
            
            ptr = ptr_ = aPtr;
            if (ptr_) 
            {
                next_ = ptr->loose_ptr_head_;
                ptr->loose_ptr_head_ = this;
            }
        }
    }
};


//****************************************************************************
//
//  Class:      enable_loose_ptr<T>
//
//  Purpose:    Required base class for objects that can have loose pointers to them.
//
//****************************************************************************
template<class TYPE> 
class enable_loose_ptr 
{
public:
    /// Default constructor
    enable_loose_ptr() : loose_ptr_head_(0)
    {
    }
    /// Copy constructor
    enable_loose_ptr(const enable_loose_ptr&) : loose_ptr_head_(0)
    {
    }
    /// Destructor
    ~enable_loose_ptr()
    {
        while (loose_ptr_head_)
            loose_ptr_head_->set(0);
    }
    /// Assignment operator
    enable_loose_ptr& operator=(const enable_loose_ptr&)
    {
    }
private:
    friend class loose_ptr<TYPE>;
    mutable loose_ptr<TYPE>* loose_ptr_head_;
};

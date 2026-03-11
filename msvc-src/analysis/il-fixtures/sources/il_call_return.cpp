// Call/return shape fixture: IL CALL_START/CALL_EXEC, RET, tail call patterns.
// Tests: simple call, virtual call, chained return, void vs value return.

struct Base {
    virtual int vfunc(int x);
    virtual void vvoid();
};

extern int plain_func(int x, int y);
extern void void_func(int x);
extern int get_value();

// Simple call with return
int call_and_return(int x) {
    return plain_func(x, x + 1);
}

// Tail call (return of call result directly)
int tail_call(int x) {
    return plain_func(x, 0);
}

// Multiple calls, return last
int chain_calls(int x) {
    void_func(x);
    return plain_func(x, get_value());
}

// Virtual call
int virtual_call(Base* obj, int x) {
    return obj->vfunc(x);
}

// Virtual void call
void virtual_void_call(Base* obj) {
    obj->vvoid();
}

// Conditional return
int conditional_return(int x) {
    if (x > 0) {
        return plain_func(x, 1);
    }
    return 0;
}

// Void function with early return
void early_return(int x) {
    if (x == 0) return;
    void_func(x);
    void_func(x + 1);
}

// Return value stored in local
int cached_return(int x) {
    int result = plain_func(x, 0);
    void_func(result);
    return result;
}

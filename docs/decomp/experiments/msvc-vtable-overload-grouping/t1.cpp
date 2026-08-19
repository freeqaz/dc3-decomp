struct ADSR {}; struct ADSRImpl {};
class Base {
public:
    virtual ~Base();
    virtual void A();
    virtual void SetADSR(int, const ADSR &);
    virtual void B();
};
// CASE 1: derived redeclares the base overload AND adds a new overload of the same name.
class D1 : public Base {
public:
    virtual ~D1();
    virtual void A();
    virtual void SetADSR(int, const ADSR &) {}   // override, base slot
    virtual void B();
    virtual void NewOne();                        // new virtual, declared BEFORE SetADSR(ADSRImpl)
    virtual void SetADSR(int, const ADSRImpl &);  // new virtual, same NAME as an override
    virtual void NewTwo();                        // new virtual
};
D1::~D1(){} void D1::A(){} void D1::B(){} void D1::NewOne(){} void D1::NewTwo(){}
void D1::SetADSR(int, const ADSRImpl &){}
D1 g1;

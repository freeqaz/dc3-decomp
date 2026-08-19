struct ADSR {}; struct ADSRImpl {};
class Base { public:
    virtual ~Base(); virtual void A(); virtual void SetADSR(int, const ADSR &); virtual void B(); };
// CASE 3: the redundant override is declared AFTER NewOne but BEFORE NewTwo.
class D1 : public Base { public:
    virtual ~D1();
    virtual void A();
    virtual void B();
    virtual void NewOne();
    virtual void SetADSR(int, const ADSR &) {}    // override -- first mention of the NAME
    virtual void NewTwo();
    virtual void SetADSR(int, const ADSRImpl &);  // new virtual, declared LAST
    virtual void NewThree();
};
D1::~D1(){} void D1::A(){} void D1::B(){} void D1::NewOne(){} void D1::NewTwo(){} void D1::NewThree(){}
void D1::SetADSR(int, const ADSRImpl &){}
D1 g1;

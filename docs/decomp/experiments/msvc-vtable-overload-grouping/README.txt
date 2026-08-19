MSVC (Xenon cl.exe 16.00.11886.00) vtable slot assignment for overloaded virtuals
=================================================================================

Build each with the project's own compiler and read the ??_7D1@@6B@ COMDAT:

  wibo build/compilers/X360/16.00.11886.00/cl.exe /nologo /c /GR /O1 /EHsc /TP \
       /FAsc /FaN.asm /FoN.obj tN.cpp

RULE (measured, not inferred): MSVC assigns vtable slots for ALL virtual member
functions sharing a name as one GROUP, positioned at that name's FIRST
declaration in the class.  A new virtual overload therefore takes its slot
where the *name* first appears, not where that overload is declared.

t1.cpp  override SetADSR(int,const ADSR&) declared FIRST, then NewOne,
        then the new overload SetADSR(int,const ADSRImpl&), then NewTwo
   -> new-virtual block = [SetADSR(ADSRImpl), NewOne, NewTwo]

t2.cpp  CONTROL: identical but with the redundant override deleted
   -> new-virtual block = [NewOne, SetADSR(ADSRImpl), NewTwo]

t3.cpp  GENERALISATION: the override is declared in the MIDDLE (after NewOne)
   -> new-virtual block = [NewOne, SetADSR(ADSRImpl), NewTwo, NewThree]
      i.e. the new overload lands at the OVERRIDE's position, not at the front.

So "MSVC hoists a new virtual to the front of the derived new-virtual block"
is only the special case where the override precedes every other new virtual.
t3 shows the general form.

Instance in DC3: StandardStream declared a redundant
`virtual void SetADSR(int, const ADSR&) {}` override (Stream's own body is
already an empty ICF-folded stub, and ham_xbox_r.map has NO
?SetADSR@StandardStream@@UAAXHABVADSR@@@Z, so retail never had it).  That
declaration dragged SetADSR(int, const ADSRImpl&) up to slot +0xc8, which the
target gives to ?GetChannel@StandardStream@@UBAPAVStreamReceiver@@H@Z, and
shifted every slot below it.

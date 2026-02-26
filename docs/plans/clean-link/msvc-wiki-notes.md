

'''Visual C++ name mangling''' is a [[name mangling|mangling]] (decoration) scheme used in [[Microsoft]]'s [[Visual C++]] series of [[compiler]]s. It provides a way of encoding the name and additional information about a [[function (programming)|function]], [[structure]], [[class (computer science)|class]] or another [[datatype]] in order to pass more semantic information from the Microsoft Visual C++ compiler to its [[Linker (computing)|linker]]. Visual Studio and the [[Windows SDK]] (which includes the command line compilers) come with the program <code>undname</code>, which may be invoked to obtain the C-style function prototype encoded in a mangled name. The information below has been mostly reverse-engineered; there is no official documentation for the actual algorithm used.

==Overview==

Any [[object code]] produced by the compiler is usually linked with other pieces of object code by the linker. The linker relies on unique object names for identification but C++ (and many modern programming languages) allows different entities to be named with the same [[identifier]] as long as they occupy a different [[namespace]]. Names need to be mangled by the compiler to make them distinct before reaching the linker. The linker also needs information on each program entity. For example, to correctly link a function it needs its name, the number of arguments and their types. C++ decoration can become complex (storing information about classes, templates, namespaces, [[operator overloading]], etc.). 

The [[C++]] [[programming language|language]] does not define a standard decoration scheme, so each C++ compiler uses its own.

==Basic Structure==
All mangled C++ names start with <code>?</code> (question mark). Because all mangled [[C (programming language)|C]] names start with alphanumeric characters, <code>@</code> (at-sign) and <code>_</code> (underscore), C++ names can be distinguished from C names.

The structure of mangled names looks like this:
* Prefix <code>?</code>
* ''Optional'': Prefix <code>@?</code>
* Qualified name
* Type information (see below)

===Function===
Type information in function names generally looks like this:
* Access level and function type
* ''Conditional'': CV-class modifier of function, if non-static member function
* Function property

===Data===
Type information in data names looks like this:
* Access level and storage class
* Data type
* CV-class modifier

==Elements==
Mangled name contains a lot of elements which will be discussed.

===Name===
Qualified name consists of the following fragments:
* Basic name: one of: [[#Name Fragment|name fragment]] or [[#Special Name|special name]]
* Qualification #1: one of: [[#Name Fragment|name fragment]], [[#Name with Template Arguments|name with template arguments]], [[#Numbered Namespace|numbered namespace]] or [[#Back Reference|back reference]]
* Qualification #2
* ...
* Terminator <code>@</code>

Qualification is written in reversed order. For example <code>myclass::nested::something</code> becomes <code>something@nested@myclass@@</code>.

====Name Fragment====
A fragment of a name is simply represented as the name with trailing <code>@</code>.

====Special Name====
Special names are represented as a code with a preceding <code>?</code>. Most of special names are constructor, destructor, operator and internal symbol. Below is a table for known codes.
{| class="wikitable sortable"
|-
! Code
! Meaning
|-
! <code>0</code>
| Constructor
|-
! <code>1</code>
| Destructor
|-
! <code>2</code>
| <var>operator new</var>
|-
! <code>3</code>
| <var>operator delete</var>
|-
! <code>4</code>
| <var>operator =</var>
|-
! <code>5</code>
| <var>operator &gt;&gt;</var>
|-
! <code>6</code>
| <var>operator &lt;&lt;</var>
|-
! <code>7</code>
| <var>operator !</var>
|-
! <code>8</code>
| <var>operator ==</var>
|-
! <code>9</code>
| <var>operator !=</var>
|-
! <code>A</code>
| <var>operator[]</var>
|-
! <code>B</code>
| <var>operator returntype</var>
|-
! <code>C</code>
| <var>operator -&gt;</var>
|-
! <code>D</code>
| <var>operator *</var>
|-
! <code>E</code>
| <var>operator ++</var>
|-
! <code>F</code>
| <var>operator --</var>
|-
! <code>G</code>
| <var>operator -</var>
|-
! <code>H</code>
| <var>operator +</var>
|-
! <code>I</code>
| <var>operator &amp;</var>
|-
! <code>J</code>
| <var>operator -&gt;*</var>
|-
! <code>K</code>
| <var>operator /</var>
|-
! <code>L</code>
| <var>operator %</var>
|-
! <code>M</code>
| <var>operator &lt;</var>
|-
! <code>N</code>
| <var>operator &lt;=</var>
|-
! <code>O</code>
| <var>operator &gt;</var>
|-
! <code>P</code>
| <var>operator &gt;=</var>
|-
! <code>Q</code>
| <var>operator, </var>
|-
! <code>R</code>
| <var>operator ()</var>
|-
! <code>S</code>
| <var>operator ~</var>
|-
! <code>T</code>
| <var>operator ^</var>
|-
! <code>U</code>
| <var>operator &#124;</var>
|-
! <code>V</code>
| <var>operator &amp;&amp;</var>
|-
! <code>W</code>
| <var>operator &#124;&#124;</var>
|-
! <code>X</code>
| <var>operator *=</var>
|-
! <code>Y</code>
| <var>operator +=</var>
|-
! <code>Z</code>
| <var>operator -=</var>
|-
! <code>_0</code>
| <var>operator /=</var>
|-
! <code>_1</code>
| <var>operator %=</var>
|-
! <code>_2</code>
| <var>operator &gt;&gt;=</var>
|-
! <code>_3</code>
| <var>operator &lt;&lt;=</var>
|-
! <code>_4</code>
| <var>operator &amp;=</var>
|-
! <code>_5</code>
| <var>operator &#124;=</var>
|-
! <code>_6</code>
| <var>operator ^=</var>
|-
! <code>_7</code>
| 'vftable'
|-
! <code>_8</code>
| 'vbtable'
|-
! <code>_9</code>
| 'vcall'
|-
! <code>_A</code>
| 'typeof'
|-
! <code>_B</code>
| 'local static guard'
|-
! <code>_C</code>
| ''String constant (see below)''
|-
! <code>_D</code>
| 'vbase destructor'
|-
! <code>_E</code>
| 'vector deleting destructor'
|-
! <code>_F</code>
| 'default constructor closure'
|-
! <code>_G</code>
| 'scalar deleting destructor'
|-
! <code>_H</code>
| 'vector constructor iterator'
|-
! <code>_I</code>
| 'vector destructor iterator'
|-
! <code>_J</code>
| 'vector vbase constructor iterator'
|-
! <code>_K</code>
| 'virtual displacement map'
|-
! <code>_L</code>
| 'eh vector constructor iterator'
|-
! <code>_M</code>
| 'eh vector destructor iterator'
|-
! <code>_N</code>
| 'eh vector vbase constructor iterator'
|-
! <code>_O</code>
| 'copy constructor closure'
|-
! <code>_P</code>
| 'udt returning' ''(prefix)''
|-
! <code>_Q</code>
| ''Unknown''
|-
! <code>_R</code>
| ''RTTI-related code (see below)''
|-
! <code>_S</code>
| 'local vftable'
|-
! <code>_T</code>
| 'local vftable constructor closure'
|-
! <code>_U</code>
| <var>operator new[]</var>
|-
! <code>_V</code>
| <var>operator delete[]</var>
|-
! <code>_W</code>
| 'omni callsig'
|-
! <code>_X</code>
| 'placement delete closure'
|-
! <code>_Y</code>
| 'placement delete[] closure'
|-
! <code>__A</code>
| 'managed vector constructor iterator'
|-
! <code>__B</code>
| 'managed vector destructor iterator'
|-
! <code>__C</code>
| 'eh vector copy constructor iterator'
|-
! <code>__D</code>
| 'eh vector vbase copy constructor iterator'
|-
! <code>__E</code>
| 'dynamic initializer' ''(Used by CRT entry point to construct non-trivial? global objects)''
|-
! <code>__F</code>
| 'dynamic atexit destructor` ''(Used by CRT to destroy non-trivial? global objects on program exit)''
|-
! <code>__G</code>
| 'vector copy constructor iterator'
|-
! <code>__H</code>
| 'vector vbase copy constructor iterator'
|-
! <code>__I</code>
| 'managed vector copy constructor iterator'
|-
! <code>__J</code>
| 'local static thread guard'
|-
! <code>__K</code>
| user-defined literal operator
|}

Below are the [[RTTI]]-related codes (all starting with <code>_R</code>). Some codes have trailing parameters.
{| class="wikitable"
|-
! Code
! Meaning
! Trailing Parameters
|-
! <code>_R0</code>
| ''type'' 'RTTI Type Descriptor'
| Data type ''type''.
|-
! <code>_R1</code>
| 'RTTI Base Class Descriptor at (''a'',''b'',''c'',''d'')'
| Four encoded numbers: ''a'', ''b'', ''c'' and ''d''.
|-
! <code>_R2</code>
| 'RTTI Base Class Array'
| None.
|-
! <code>_R3</code>
| 'RTTI Class Hierarchy Descriptor'
| None.
|-
! <code>_R4</code>
| 'RTTI Complete Object Locator'
| None.
|}
String constants (all starting with <code>_C@_</code>):

The name corresponds to the value stored in a read-only COMDAT section, in order to avoid duplicate storage of the same string.
These sections are generated only if the '''/GF''' switch is given to the Microsoft compiler.

The entire name consists of:
* <code>_C@_0</code> or <code>_C@_1</code>.  Indicates single- or double-byte characters, respectively.
* Length of the string in ''bytes'' (encoded number).  Includes null terminating character, if any.
* A 32-bit value (encoded number).  Meaning unknown, presumably a hash of the string.
* The ''bytes'' of the string (up to the ''first 32 characters'' only).  For double-byte strings, the bytes are in big-endian order.  They can be interpreted as Unicode text using the '''UTF-16BE''' encoding.  Each byte is encoded as:
{| class="wikitable"
!Code
!meaning
|-
|?$''xx''
|2 hex digits encoded as A (which means 0) to P (15).
|-
|?0-9
|corresponding char in string <code>",/\:. {ctrl-K}{ctrl-J}'-"</code>.
|-
|?a-p or ?A-P
|corresponding ASCII char + hex 80.
|-
|''(other)''
|the actual character
|}
* Possibly another encoded number, meaning unknown.
* Terminating <code>@</code> character.
For example, the complete name <code>_C@_1CK@EOPGIILJ@?$AAi?$AAn?$AAv?$AAa?$AAl?$AAi?$AAd?$AA?5?$AAn?$AAu?$AAl?$AAl?$AA?5?$AAp?$AAo?$AAi?$AAn?$AAt?$AAe?$AAr?$AA?$AA@</code> represents the 21-character double-byte string "invalid null pointer\0".  All characters have 0 for their high order byte.

It is possible, but very unlikely, for two different strings to be given the same symbol name.  The strings would have to have the same first 32 characters, the same length, and the same hash value.  The MSVC compiler generates COMDAT sections which tell the linker to "pick any" section with the same symbol name, ignoring the contents.  Therefore, the linker will not catch the discrepancy.

====Name with Template Arguments====
Name fragments starting with <code>?$</code> have template arguments. This kind of name looks like this:
* Prefix <code>?$</code>
* Name terminated by <code>@</code>
* Template argument list

For example, we assume the following prototype.
<syntaxhighlight lang="cpp">
void __cdecl abc<def<int>,void*>::xyz(void);
</syntaxhighlight>

The name of this function can be obtained by the following process:
<syntaxhighlight lang="cpp">
abc<def<int>,void*>::xyz
xyz@ abc<def<int>,void*> @
xyz@ ?$abc@ def<int> void* @ @
xyz@ ?$abc@ V def<int> @ PAX @ @
xyz@ ?$abc@ V ?$def@H@ @ PAX @ @
xyz@?$abc@V?$def@H@@PAX@@
</syntaxhighlight>

So the mangled name for this function is <code>?xyz@?$abc@V?$def@H@@PAX@@YAXXZ</code>.

====Nested Name====
A name fragment starting with <code>??</code> denotes a nested name. This is a name inside a local scope which must be exported. Its structure looks like the following:
* Optional sequence number for multiple occurrences of same name in the same local scope.  This can only happen if the scope is a function, with the name being declared in multiple blocks.  It consists of:
** <code>?</code>
** encoded number.
* Prefix <code>?</code>
* C++ Mangled name (so starting with <code>?</code> again), which names the local scope.

For example, <code>?nested@??func@@YAXXZ@4HA</code> means variable <code>?nested@@4HA(int nested)</code> inside <code>?func@@YAXXZ(void __cdecl func(void))</code>. The UnDecorateSymbolName function returns <code>int `void __cdecl func(void)'::nested</code> for this input.

And <code>?CONST@?1??main@@9@4HB</code> means constant <code>?CONST@@4HB (const int CONST)</code> inside <code>main@@9 (main)</code>, where the compiler chose the number 2 to associate with it.  The UnDecorateSymbolName function returns <code>int const `main'::`2'::CONST</code>for this input.

====Numbered Namespace====
In qualification, a numbered namespace is represented as a preceding <code>?</code> and an unsigned number. The UnDecorateSymbolName function returns something like '<code>42</code>' for this kind of input.

Exceptionally, if a numbered namespace starts with <code>?A</code>, it becomes an anonymous namespace ('<code>anonymous namespace</code>').

====Back Reference====
Decimal digits 0 to 9 refer to the first through 10th shown name fragments. Referred name fragments can be normal name fragments or name fragments with template arguments. For example, in <code>alpha@?1beta@@(beta::'2'::alpha)</code>, 0 refers to <code>alpha@</code>, and 1 (not 2) refers to <code>beta@</code>.

Generally, the back reference table is kept during the entire mangling process. This means you can use a back reference to the function name in the function arguments (which appear after the function name). However, in the template argument list, the back reference table is separately created.

For example, assume <code>?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@</code> (<code>std::basic_string<unsigned short, std::char_traits<unsigned short>, std::allocator<unsigned short> ></code>). In <code>std::basic_string<...></code>, 0 refers to <code>basic_string@</code>, 1 refers to <code>?$char_traits@G@</code>, and 2 refers to <code>std@</code>. This relation doesn't change wherever it appears.

===Encoded Number===
In name mangling, sometimes numbers must be represented (e.g. array indices). There are simple rules for this:
* <code>0</code> to <code>9</code> represents numbers 1 to 10.
* <code>num@</code> represents a hexadecimal number, where ''num'' consists of hexadecimal digits A (which means 0) to P (15). For example <code>BCD@</code> means number 0x123, or 291 in decimal notation.
* <code>A@</code> represents the number 0.
* If allowed, the prefix <code>?</code> represents a minus sign. Note that both <code>?@</code> and <code>@</code> represent number 0.

===Data Type===
The table below shows the various data type and modifiers.
{| class="wikitable"
|-
! Code
! Meaning with no underline
! Meaning with preceding underline
! Meaning with preceding <code>$$</code>
|-
! ?
| ''Type modifier, Template parameter''
|
|
|-
! $
| ''Type modifier, Template parameter''
| __w64 ''(prefix)''
|
|-
! 0-9
| ''Back reference''
|
|
|-
! A
| ''Type modifier (reference)''
|
| ''Type modifier (function)''{{ref|funct}}
|-
! B
| ''Type modifier (volatile reference)''
|
| ''Array type in template''
|-
! C
| signed char
|
| ''Type modifier''
|-
! D
| char
| __int8
|
|-
! E
| unsigned char
| unsigned __int8
|
|-
! F
| short
| __int16
| ''Function modifier (managed function [Managed C++ or C++/CLI])''{{ref|ddf}}
|-
! G
| unsigned short
| unsigned __int16
|
|-
! H
| int
| __int32
|
|-
! I
| unsigned int
| unsigned __int32
|
|-
! J
| long
| __int64
|
|-
! K
| unsigned long
| unsigned __int64
|
|-
! L
|
| __int128
|
|-
! M
| float
| unsigned __int128
|
|-
! N
| double
| bool
|
|-
! O
| long double
| ''Array''
|
|-
! P
| ''Type modifier (pointer)''
|
|
|-
! Q
| ''Type modifier (const pointer)''
|char8_t
| ''Type modifier (rvalue reference)''
|-
! R
| ''Type modifier (volatile pointer)''
|
| ''Type modifier (volatile rvalue reference)''
|-
! S
| ''Type modifier (const volatile pointer)''
| char16_t
|
|-
! T
| ''Complex Type (union)''
|
| std::nullptr_t
|-
! U
| ''Complex Type (struct)''
| char32_t
| 
|-
! V
| ''Complex Type (class)''
|
| Empty type parameter pack
|-
! W
| ''Enumerate Type (enum)''
| wchar_t
|
|-
! X
| void, ''Complex Type (coclass)''
| ''Complex Type (coclass)''
|
|-
! Y
| ''Complex Type (cointerface)''
| ''Complex Type (cointerface)''
|
|-
! Z
| ... ''(ellipsis)''
|
| End template parameter pack
|}
[[#ref_funct|^]] Visible when function is passed to <code>typeid</code> operator.  Uses pointer type syntax.

[[#ref_ddf|^]] See [[#Function_2|Function section]].

The code <code>X</code> represents <code>void</code> when it appears in as a return type or pointer type, otherwise it indicates a cointerface. The code <code>Z</code> (meaning ellipsis) appears only at the end of an argument list.

====Primitive & Extended Type====
Primitive types are represented as one character, and extended types are represented as one character with a preceding <code>_</code>.

====Back Reference====
Decimal digits <code>0</code> to <code>9</code> refer to the first through 10th shown type in the argument list. (This means return type cannot be a referent.) Back references can refer to any non-primitive type, including an extended type. Of course back references can refer to prefixed types such as <code>PAVblah@@</code>(<code>class blah *</code>), but cannot refer to prefixless types — say, <code>Vblah@@</code> in <code>PAVblah@@</code>.

With back references for names, in a template argument list the back reference table is separately created. The function argument list has no such scoping rule, though it can be confuseing sometimes. For example, assume <code>P6AXValpha@@Vbeta@@@Z</code>(<code>void (__cdecl*)(class alpha, class beta)</code>) is the first shown non-primitive type. Then <code>0</code> refers to <code>Valpha@@</code>, <code>1</code> refers to <code>Vbeta@@</code>, and finally <code>2</code> refers to 'function pointer'.

====Type Modifier====
A type modifier is used to make a pointer or reference. Type modifiers look like this:
* Modifier type
* ''Optional:'' Managed C++ property (<code>$A</code> for <code>__gc</code>, <code>$B</code> for <code>__pin</code>)
* CV-class modifier
* ''Optional:'' Array property (not for functions)
** Prefix Y
** Encoded unsigned number of dimensions
** Array indices as encoded unsigned numbers, one for each dimension
* Referred type info (see below)

There are ten types of type modifier:
{| class="wikitable"
|-
!
! ''none''
! const
! volatile
! const volatile
|-
! '''Pointer'''
| <code>P</code>
| <code>Q</code>
| <code>R</code>
| <code>S</code>
|-
! '''Reference'''
| <code>A</code>
|
| <code>B</code>
|
|-
! '''Rvalue Reference'''
| <code>$$Q</code>
|
| <code>$$R</code>
|
|-
! '''''none'''''
| <code>?</code>, <code>$$C</code>
|
|
|
|}

For normal types, referred type info is data type. For functions, it looks like the following. (It depends on the CV-class modifier)
* Conditional: CV-class modifier, if member function
* Function property

====Complex Type (union, struct, class, coclass, cointerface)====
Complex types look like this:
* Kind of complex type (<code>T</code>, <code>U</code>, <code>V</code>, ...)
* Qualification without a basic name

====Enumerated Type (enum)====
An enumerated type starts with the prefix <code>W</code>. It looks like this:
* Prefix <code>W</code>
* Real type for enum
* Qualification without basic name

The real type for an enum is represented as follows:
{| class="wikitable"
|-
! Code
! Corresponding Real Type
|-
! <code>0</code>
| char
|-
! <code>1</code>
| unsigned char
|-
! <code>2</code>
| short
|-
! <code>3</code>
| unsigned short
|-
! <code>4</code>
| int ''(generally normal "enum")''
|-
! <code>5</code>
| unsigned int
|-
! <code>6</code>
| long
|-
! <code>7</code>
| unsigned long
|}

Note that in modern versions of Visual Studio, it will usually (if not always) generate enum symbols with a type symbol of <code>W4</code>, regardless of the real underlying type.  Note that this doesn't affect the underlying type in any way, but appears to be for the sake of compiler simplicity.

====Array====
An array (not pointer to array) starts with the prefix <code>_O</code>. It looks like this:
* Prefix <code>_O</code>
* CV-class modifier
* Data type within array

You can use multi-dimensional array like <code>_OC_OBH</code>, but only the outermost CV-class modifier is affected. (In this case <code>_OC_OBH</code> means <code>int volatile [][]</code>, not <code>int const [][]</code>)

====Template Parameter====
Template parameters are used to represent type and non-type template arguments. They can be used only in a template argument list.

The table below is a list of known template parameters. ''a'', ''b'', ''c'' represent encoded signed numbers, and ''x'', ''y'', ''z'' represent encoded unsigned numbers.
{| class="wikitable"
|-
! Code
! Meaning
|-
! <code>?''x''</code>
| anonymous type template parameter ''x'' (<var>'template-parameter-x'</var>)
|-
! <code>$0''a''</code> 
| integer value ''a'' {{ref|pmem}}
|-
! <code>$1''s''</code>
| constant pointer to mangled symbol ''s'' {{ref|lvalue}}
|-
! <code>$2{{italic-multi|a|b}}</code>
| real value ''a'' &times; 10<sup>''b''-''k''+1</sup>, where ''k'' is number of decimal digits of ''a''
|-
! <code>$D''a''</code>
| anonymous type template parameter ''a'' (<var>'template-parameter''a'''</var>)
|-
! <code>$F{{italic-multi|a|b}}</code>
| 2-tuple {''a'',''b''} ''(unknown)''
|-
! <code>$G{{italic-multi|a|b|c}}</code>
| 3-tuple {''a'',''b'',''c''} ''(unknown)''
|-
! <code>$H{{italic-multi|s|a}}</code>
| constant pointer to method ''s'' (base offset? ''a'', numeric)
|-
! <code>$I{{italic-multi|s|a|b}}</code>
| constant pointer to method ''s'' (offsets? ''a'' and ''b'', numeric)
|-
! <code>$J{{italic-multi|x|y|z}}</code>
| ''(unknown)''
|-
! <code>$Q''a''</code>
| anonymous non-type template parameter ''a'' (<var>'non-type-template-parameter''a'''</var>)
|-
! <code>$S</code>
| empty non-type parameter pack
|}

<span id="endnote_pmem"></span>[[#ref_pmem|^]] Pointer to member variable ''v'' in ''X'' is represented as the integer <code>offsetof(''X'', ''v'')</code>

<span id="endnote_lvalue"></span>[[#ref_lvalue|^]] The pointer syntax is also used for lvalue references and pointers to member functions.

===Argument List===
An argument list is a sequence of data types. The list can be one of the following:
* <code>X</code> (means <code>void</code>, also terminating list)
* arg1 arg2 ... argN <code>@</code> (meaning a normal list of data types. Note that N can be zero)
* arg1 arg2 ... argN <code>Z</code> (meaning a list with trailing ellipsis)

====Template Argument List====
A template argument list is the same as an argument list, except that template parameters can be used.

===CV-class Modifier===
The following table shows CV-class modifiers.
{|  class="wikitable"
|-
!  rowspan="2" |
!  colspan="4" | Variable
!  rowspan="2" | Function
|-
| ''none''
| const
| volatile
| const volatile
|-
! ''none''
| <code>A</code>
| <code>B</code>, <code>J</code>
| <code>C</code>, <code>G</code>, <code>K</code>
| <code>D</code>, <code>H</code>, <code>L</code>
| <code>6</code>, <code>7</code>
|-
! __based()
| <code>M</code>
| <code>N</code>
| <code>O</code>
| <code>P</code>
| <code>_A</code>, <code>_B</code>
|-
! Member
| <code>Q</code>, <code>U</code>, <code>Y</code>
| <code>R</code>, <code>V</code>, <code>Z</code>
| <code>S</code>, <code>W</code>, <code>0</code>
| <code>T</code>, <code>X</code>, <code>1</code>
| <code>8</code>, <code>9</code>
|-
! __based() Member
| <code>2</code>
| <code>3</code>
| <code>4</code>
| <code>5</code>
| <code>_C</code>, <code>_D</code>
|}
CV-class modifier can have zero or more prefixes:
{| class="wikitable"
|-
! Prefix
! Meaning
|-
! <code>E</code>
| ''type'' __ptr64
|-
! <code>F</code>
| __unaligned ''type''
|-
! <code>G</code>
| ''type'' &
|-
! <code>H</code>
| ''type'' &&
|-
! <code>I</code>
| ''type'' __restrict
|}
Modifiers have trailing parameters as follows:
* Conditional: Qualification without basic name, if member
* Conditional: CV-class modifier of function, if member function
* Conditional: __based() property, if used

A CV-class modifier is usually used in reference/pointer types, but it is also used in other places with some restrictions:
* Modifier of function: can only have const, volatile attribute, optionally with prefixes.
* Modifier of data: cannot have function property.

===__based() Property===
__based() property represents Microsoft's __based() attribute extension to C++. This property can be one of the following:
* <code>0</code> (means <code>__based(void)</code>)
* <code>2</code>''name'' (means <code>__based(name)</code>, where name is a qualification without a basic name)
* <code>5</code> (means no <code>__based()</code>)

===Function Property===
A function property represents the prototype of a function. It looks like this:
* Calling convention of function
* Data type of returned value, or <code>@</code> for <code>void</code>
* Argument list
* throw() attribute

The following table shows calling conventions of functions:
{| class="wikitable"
|-
! Code
! Exported?
! Calling Convention
|-
! <code>A</code>
| No
| __cdecl
|-
! <code>B</code>
| Yes
| __cdecl
|-
! <code>C</code>
| No
| __pascal
|-
! <code>D</code>
| Yes
| __pascal
|-
! <code>E</code>
| No
| __thiscall
|-
! <code>F</code>
| Yes
| __thiscall
|-
! <code>G</code>
| No
| __stdcall
|-
! <code>H</code>
| Yes
| __stdcall
|-
! <code>I</code>
| No
| __fastcall
|-
! <code>J</code>
| Yes
| __fastcall
|-
! <code>K</code>
| No
| ''none''
|-
! <code>L</code>
| Yes
| ''none''
|-
! <code>M</code>
| No
| __clrcall
|}

The argument list for the <code>throw()</code> attribute is the same as any other argument list, but if this list is <code>Z</code>, it means there is no <code>throw()</code> attribute. If you want to represent <code>throw()</code> you have to use <code>@</code> to terminate the list.

==Function==
Typical type information in a function name looks like this:
* ''Optional'': Prefix <code>$$F</code> (means function is managed, either as Managed C++ or C++/CLI)
* ''Optional'': Prefix <code>_</code> (means __based() property is used)
* Access level and function type
* ''Conditional'': __based() property, if used
* ''Conditional'': adjustor property (as encoded unsigned number), if [[thunk]] function
* ''Conditional'': CV-class modifier of function, if non-static member function
* Function property

The table below shows codes for access level and function type:
{|  class="wikitable"
|-
!
! ''none''
! static
! virtual
! thunk
|-
! private:
| <code>A</code>, <code>B</code>
| <code>C</code>, <code>D</code>
| <code>E</code>, <code>F</code>
| <code>G</code>, <code>H</code>
|-
! protected:
| <code>I</code>, <code>J</code>
| <code>K</code>, <code>L</code>
| <code>M</code>, <code>N</code>
| <code>O</code>, <code>P</code>
|-
! public:
| <code>Q</code>, <code>R</code>
| <code>S</code>, <code>T</code>
| <code>U</code>, <code>V</code>
| <code>W</code>, <code>X</code>
|-
! ''none''
| <code>Y</code>, <code>Z</code>
|
|
|
|}
This kind of thunk function is always virtual, and used to represent the logical <code>this</code> adjustor property, which means an offset to the true <code>this</code> value in some multiple inheritance situations.

Two codes are assigned to each function for historical reasons: the first code was used for <code>near</code> calls, the second for <code>far</code> calls.

==Data==
Type information in a data name looks like this:
* Access level
* Data type
* CV-class modifier

The table below shows codes for access level:
{|  class="wikitable"
|-
! Code
! Meaning
|-
! 0
| Private static member
|-
! 1
| Protected static member
|-
! 2
| Public static member
|-
! 3
| Normal variable
|-
! 4
| Normal variable
|-
|}
The CV-class modifier should be appropriate for data (not a 'function' modifier).

==Thunk Function==
There are several kinds of [[thunk]] function.

==See also==
*[[Programming language]]
*[[Visual C++]]
*[[C++]]

==References==
<div class="references-small">
* {{cite web|url=http://mearie.org/documents/mscmangle|title=Microsoft C++ Name Mangling Scheme|accessdate=2008-10-05|author=Kang Seonghoon}}
</div>

==External links==
*[http://www.agner.org/optimize/calling_conventions.pdf Calling conventions for different C++ compilers] by Agner Fog contains a description of the name mangling schemes for Visual C++ x86 and x64 (pp.&nbsp;28–33 in the 2011-06-08 version)
* [http://mearie.org/documents/mscmangle Microsoft C++ Name Mangling Scheme]
* [http://www.kegel.com/mangle.html C++ Name Mangling/Demangling]
* [http://www.geoffchappell.com/viewer.htm?doc=studies/msvc/language/decoration/index.htm&tx=14,16  Geoff Chappell's results]
* [https://gitlab.winehq.org/wine/wine/-/blob/master/dlls/msvcrt/undname.c __unDname] Wine's __unDname function implementation
* [http://sourceforge.net/projects/php-ms-demangle/ PHP UnDecorateSymbolName] A PHP Script that demangles Microsoft C++ Names by Timo Stripf
* [http://msdn.microsoft.com/en-us/library/5x49w699.aspx Undname] Convert a decorated name to its undecorated form
* [https://github.com/airbus-seclab/elfesteem/blob/dev/elfesteem/visual_studio_mangling.py visual_studio_mangling.py] A Python script that demangles Microsoft C++ Names
* [https://pypi.python.org/pypi/pdbparse/1.2 undname.c]A C function that demangles Microsoft C++ Names, found in the zip file downloaded from the Python pdbparse-1.2 package.  Package also contains Python code to examine a PDB file.

[[Category:C++]]
[[Category:Programming language implementation]]


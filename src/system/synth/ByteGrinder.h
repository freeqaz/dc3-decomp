#pragma once

/** Handles MOGG/BIK encryption, as it is unique from BinStream encryption. */
class ByteGrinder {
public:
    ByteGrinder() {}
    virtual ~ByteGrinder() {} // generic dtor
    void GrindArray(int, int, unsigned char *, int, int);
    void Init();
    void HvDecrypt(unsigned char *, unsigned char *, int);

private:
    unsigned int pickOneOf32A(bool, int);
    unsigned int pickOneOf32B(bool, int);
};

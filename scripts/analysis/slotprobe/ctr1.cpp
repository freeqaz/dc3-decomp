float Sine(float);
float Clamp(float,float,float);
struct C { float a,b; int c,d; };
void v1(C*o, void*data, unsigned int size) {           // our current shape
    float z1=o->a, z2=o->b; unsigned int samps = size>>1;
    if (samps!=0){ short*p=(short*)data-1;
      for(unsigned int i=0;i!=samps;i++){ float in=(float)p[1];
        float out=(in-z1)*2.0f+z2*0.5f; z1=in; out=Clamp(-32767.0f,32767.0f,out);
        *++p=(short)out; z2=(float)(short)out; } }
    o->b=z2; o->a=z1;
}
void v2(C*o, void*data, unsigned int size) {           // index compared with <
    float z1=o->a, z2=o->b; unsigned int samps = size>>1;
    if (samps!=0){ short*p=(short*)data-1;
      for(unsigned int i=0;i<samps;i++){ float in=(float)p[1];
        float out=(in-z1)*2.0f+z2*0.5f; z1=in; out=Clamp(-32767.0f,32767.0f,out);
        *++p=(short)out; z2=(float)(short)out; } }
    o->b=z2; o->a=z1;
}
void v3(C*o, void*data, unsigned int size) {           // while over a counter declared outside
    float z1=o->a, z2=o->b; unsigned int samps = size>>1; unsigned int i=0;
    if (samps!=0){ short*p=(short*)data-1;
      do { float in=(float)p[1];
        float out=(in-z1)*2.0f+z2*0.5f; z1=in; out=Clamp(-32767.0f,32767.0f,out);
        *++p=(short)out; z2=(float)(short)out; i++; } while(i<samps); }
    o->b=z2; o->a=z1;
}
void v4(C*o, void*data, unsigned int size) {           // int counter
    float z1=o->a, z2=o->b; int samps = (int)(size>>1);
    if (samps!=0){ short*p=(short*)data-1;
      for(int i=0;i<samps;i++){ float in=(float)p[1];
        float out=(in-z1)*2.0f+z2*0.5f; z1=in; out=Clamp(-32767.0f,32767.0f,out);
        *++p=(short)out; z2=(float)(short)out; } }
    o->b=z2; o->a=z1;
}

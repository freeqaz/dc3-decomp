/* mkfilter -- given n, compute recurrence relation
   to implement Butterworth, Bessel or Chebyshev filter of order n
   A.J. Fisher, University of York   <fisher@minster.york.ac.uk>
   September 1992 */

#include "filterdesign.h"
#include "complex.h"
#include "os\Debug.h"

/* This variant uses single-precision PI (matches target codegen) */
#undef PI
#undef TWOPI
#define PI 3.1415927f
#define TWOPI (2.0 * PI)

#define opt_p 0x02000 /* -p specified poles only            */
#define opt_w 0x04000 /* -w don't pre-warp                  */
#define opt_z 0x08000 /* -z use matched z-transform         */
#define opt_Z 0x10000 /* -Z additional zero                 */

enum FilterType { kBessel = 0, kButterworth = 1, kChebyshev = 2, kResonator = 3, kProportionalIntegral = 4 };
enum FilterBand { kLowpass = 0, kHighpass = 1, kBandpass = 2, kBandstop = 3, kAllpass = 4 };

struct FILTER {
    float xcoeffs[0x200];
    float ycoeffs[0x200];
    float gain;     // 0x1000
    float gain2;    // 0x1004
    float invgain2; // 0x1008
    /* numpoles precedes numzeros -- copyresults() stores zplane.numpoles to
       +0x100c and zplane.numzeros to +0x1010.  0x100c is the field EQEffect.cpp
       reads as `numCoeffs`, and it memcpy's that many floats out of the +0x800
       array (ycoeffs), which copyresults fills with exactly numpoles entries. */
    int numpoles; // 0x100c
    int numzeros; // 0x1010
};

struct pzrep {
    complex poles[MAXPZ], zeros[MAXPZ];
    int numpoles, numzeros;
};

/* NB: MSVC emits plain (statically-initialized) file-scope statics into .bss in
   REVERSE declaration order, so this list is the target's address order read
   backwards: ycoeffs, xcoeffs, polemask, order, qfactor, chebrip, warped_alpha2,
   warped_alpha1, raw_alphaz, raw_alpha2, raw_alpha1.  In particular xcoeffs/ycoeffs
   must be declared LAST (as in upstream mkfilter) or every access to the scalars
   below is reached at the wrong displacement from the ycoeffs anchor.
   splane/zplane/dc_gain/fc_gain/hf_gain have a user-provided complex ctor, so they
   are dynamically initialized and land in a separate group in FORWARD order. */
static double raw_alpha1, raw_alpha2, raw_alphaz;
static double warped_alpha1, warped_alpha2;
static double chebrip, qfactor;
static int order;
static uint polemask;
static double xcoeffs[MAXPZ + 1], ycoeffs[MAXPZ + 1];
static pzrep splane, zplane;
static complex dc_gain, fc_gain, hf_gain;

static c_complex bessel_poles[] = {
    /* table produced by /usr/fisher/bessel -- N.B. only one member of each C.Conj. pair
       is listed */
    { -1.00000000000e+00, 0.00000000000e+00 }, { -1.10160133059e+00, 6.36009824757e-01 },
    { -1.32267579991e+00, 0.00000000000e+00 }, { -1.04740916101e+00, 9.99264436281e-01 },
    { -1.37006783055e+00, 4.10249717494e-01 }, { -9.95208764350e-01, 1.25710573945e+00 },
    { -1.50231627145e+00, 0.00000000000e+00 }, { -1.38087732586e+00, 7.17909587627e-01 },
    { -9.57676548563e-01, 1.47112432073e+00 }, { -1.57149040362e+00, 3.20896374221e-01 },
    { -1.38185809760e+00, 9.71471890712e-01 }, { -9.30656522947e-01, 1.66186326894e+00 },
    { -1.68436817927e+00, 0.00000000000e+00 }, { -1.61203876622e+00, 5.89244506931e-01 },
    { -1.37890321680e+00, 1.19156677780e+00 }, { -9.09867780623e-01, 1.83645135304e+00 },
    { -1.75740840040e+00, 2.72867575103e-01 }, { -1.63693941813e+00, 8.22795625139e-01 },
    { -1.37384121764e+00, 1.38835657588e+00 }, { -8.92869718847e-01, 1.99832584364e+00 },
    { -1.85660050123e+00, 0.00000000000e+00 }, { -1.80717053496e+00, 5.12383730575e-01 },
    { -1.65239648458e+00, 1.03138956698e+00 }, { -1.36758830979e+00, 1.56773371224e+00 },
    { -8.78399276161e-01, 2.14980052431e+00 }, { -1.92761969145e+00, 2.41623471082e-01 },
    { -1.84219624443e+00, 7.27257597722e-01 }, { -1.66181024140e+00, 1.22110021857e+00 },
    { -1.36069227838e+00, 1.73350574267e+00 }, { -8.65756901707e-01, 2.29260483098e+00 },
};

static void compute_s(FilterType type);
static void choosepole(complex z);
static void applyWarp(bool doWarp);
static void normalize(FilterBand band);
static void compute_z_blt();
static complex blt(complex pz);
static void compute_z_mzt();
static void compute_apres();
static void compute_bpres();
static void add_extra_zero();
static void expandpoly();
static void expand(complex pz[], int npz, complex coeffs[]);
static void multin(complex w, int npz, complex coeffs[]);
static void copyresults(FilterBand band, FILTER *out);

/* compute S-plane poles for prototype LP filter */
static void compute_s(FilterType type) {
    splane.numpoles = 0;
    if (type == kBessel) { /* Bessel filter */
        int p = (order * order) / 4; /* ptr into table */
        if (order & 1)
            choosepole(bessel_poles[p++]);
        for (int i = 0; i < order / 2; i++) {
            choosepole(bessel_poles[p]);
            choosepole(cconj(bessel_poles[p]));
            p++;
        }
    }
    if (type == kButterworth || type == kChebyshev) { /* Butterworth filter */
        for (int i = 0; i < 2 * order; i++) {
            double theta = (order & 1) ? (i * PI) / order : ((i + 0.5) * PI) / order;
            choosepole(expj(theta));
        }
    }
    if (type == kChebyshev) { /* modify for Chebyshev (p. 136 DeFatta et al.) */
        if (chebrip >= 0.0) {
            MILO_NOTIFY("mkfilter: Chebyshev ripple is positive; must be .lt. 0.0");
            return;
        }
        double rip = pow(10.0, -chebrip / 10.0);
        double eps = sqrt(rip - 1.0);
        double y = asinh(1.0 / eps) / (double)order;
        if (y <= 0.0) {
            MILO_NOTIFY("Bug: Chebyshev y; must be .gt. 0.0");
            return;
        }
        for (int i = 0; i < splane.numpoles; i++) {
            splane.poles[i].re *= sinh(y);
            splane.poles[i].im *= cosh(y);
        }
    }
}

static void choosepole(complex z) {
    if (z.re < 0.0) {
        if (polemask & 1)
            splane.poles[splane.numpoles++] = z;
        polemask >>= 1;
    }
}

/* for bilinear transform, perform pre-warp on alpha values */
static void applyWarp(bool doWarp) {
    if (!doWarp) {
        warped_alpha1 = raw_alpha1;
        warped_alpha2 = raw_alpha2;
    } else {
        warped_alpha1 = tan(PI * raw_alpha1) / PI;
        warped_alpha2 = tan(PI * raw_alpha2) / PI;
    }
}

/* called for trad, not for -Re or -Pi */
static void normalize(FilterBand band) {
    double w1 = TWOPI * warped_alpha1;
    double w2 = TWOPI * warped_alpha2;
    /* transform prototype into appropriate filter type (lp/hp/bp/bs) */
    switch (band) {
    /* NB: unlike upstream mkfilter, DC3 fuses the pole loop and the zero loop
       of each band into a SINGLE loop. Both loops ran `i < splane.numpoles`
       and neither touched numpoles, so this is value-identical -- but it is
       not codegen-identical: fused, the zero constants become loop-invariant
       across the whole body and MSVC hoists them into stack temps ahead of it,
       instead of pinning 0.0 (and w0) in callee-saved FPRs across the calls in
       the pole loop. */
    case kBandstop: {
        double w0 = sqrt(w1 * w2), bw = w2 - w1;
        for (int i = 0; i < splane.numpoles; i++) {
            complex hba = 0.5 * (bw / splane.poles[i]);
            complex temp = csqrt(1.0 - sqr(w0 / hba));
            splane.poles[i] = hba * (1.0 + temp);
            splane.poles[splane.numpoles + i] = hba * (1.0 - temp);
            /* also 2N zeros at (0, +-w0) */
            splane.zeros[i] = complex(0.0, +w0);
            splane.zeros[splane.numpoles + i] = complex(0.0, -w0);
        }
        splane.numpoles *= 2;
        splane.numzeros = splane.numpoles;
        break;
    }

    case kBandpass: {
        double w0 = sqrt(w1 * w2), bw = w2 - w1;
        for (int i = 0; i < splane.numpoles; i++) {
            complex hba = 0.5 * (splane.poles[i] * bw);
            complex temp = csqrt(1.0 - sqr(w0 / hba));
            splane.poles[i] = hba * (1.0 + temp);
            splane.poles[splane.numpoles + i] = hba * (1.0 - temp);
            /* also N zeros at (0,0) */
            splane.zeros[i] = 0.0;
        }
        splane.numzeros = splane.numpoles;
        splane.numpoles *= 2;
        break;
    }

    case kHighpass: {
        for (int i = 0; i < splane.numpoles; i++) {
            splane.poles[i] = w1 / splane.poles[i];
            /* also N zeros at (0,0) */
            splane.zeros[i] = 0.0;
        }
        splane.numzeros = splane.numpoles;
        break;
    }

    case kLowpass: {
        for (int i = 0; i < splane.numpoles; i++)
            splane.poles[i] = splane.poles[i] * w1;
        splane.numzeros = 0;
        break;
    }
    default:
        break;
    }
}

/* given S-plane poles & zeros, compute Z-plane poles & zeros, by bilinear transform */
static void compute_z_blt() {
    int i;
    zplane.numpoles = splane.numpoles;
    zplane.numzeros = splane.numzeros;
    for (i = 0; i < zplane.numpoles; i++)
        zplane.poles[i] = blt(splane.poles[i]);
    for (i = 0; i < zplane.numzeros; i++)
        zplane.zeros[i] = blt(splane.zeros[i]);
    while (zplane.numzeros < zplane.numpoles)
        zplane.zeros[zplane.numzeros++] = -1.0;
}

static complex blt(complex pz) { return (2.0 + pz) / (2.0 - pz); }

/* given S-plane poles & zeros, compute Z-plane poles & zeros, by matched z-transform */
static void compute_z_mzt() {
    int i;
    zplane.numpoles = splane.numpoles;
    zplane.numzeros = splane.numzeros;
    for (i = 0; i < zplane.numpoles; i++)
        zplane.poles[i] = cexp(splane.poles[i]);
    for (i = 0; i < zplane.numzeros; i++)
        zplane.zeros[i] = cexp(splane.zeros[i]);
}

static complex reflect(complex z) {
    complex r = hypot(z);
    return z / sqr(r);
}

/* compute Z-plane pole & zero positions for allpass resonator */
static void compute_apres() {
    compute_bpres(); /* iterate to place poles */
    zplane.zeros[0] = reflect(zplane.poles[0]);
    zplane.zeros[1] = reflect(zplane.poles[1]);
}

/* compute Z-plane pole & zero positions for bandpass resonator */
static void compute_bpres() {
    zplane.numpoles = zplane.numzeros = 2;
    zplane.zeros[0] = 1.0;
    zplane.zeros[1] = -1.0;
    complex topcoeffs[MAXPZ + 1];
    expand(zplane.zeros, zplane.numzeros, topcoeffs);
    /* where we want the peak to be */
    double theta = TWOPI * raw_alpha1;
    double r = exp(-theta / (2.0 * qfactor));
    double thm = theta, th1 = 0.0, th2 = PI;
    bool cvg = false;
    for (int i = 0; i < 50 && !cvg; i++) {
        complex zp = r * expj(thm);
        zplane.poles[0] = zp;
        zplane.poles[1] = cconj(zp);
        complex botcoeffs[MAXPZ + 1];
        expand(zplane.poles, zplane.numpoles, botcoeffs);
        complex g =
            evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, expj(theta));
        /* approx to atan2 */
        double phi = g.im / g.re;
        if (phi > 0.0)
            th2 = thm;
        else
            th1 = thm;
        if (fabs(phi) < EPS)
            cvg = true;
        thm = 0.5 * (th1 + th2);
    }
    unless(cvg) MILO_NOTIFY("Warning: failed to converge");
}

static void add_extra_zero() {
    if (zplane.numzeros + 2 > MAXPZ) {
        MILO_NOTIFY("Too many zeros; can't do -Z");
        return;
    }
    double theta = TWOPI * raw_alphaz;
    complex zz = expj(theta);
    zplane.zeros[zplane.numzeros++] = zz;
    zplane.zeros[zplane.numzeros++] = cconj(zz);
    /* ensure causality */
    while (zplane.numpoles < zplane.numzeros)
        zplane.poles[zplane.numpoles++] = 0.0;
}

/* given Z-plane poles & zeros, compute top & bot polynomials in Z, and then recurrence
 * relation */
static void expandpoly() {
    complex topcoeffs[MAXPZ + 1], botcoeffs[MAXPZ + 1];
    int i;
    expand(zplane.zeros, zplane.numzeros, topcoeffs);
    expand(zplane.poles, zplane.numpoles, botcoeffs);
    dc_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, 1.0);
    /* "jwT" for centre freq. */
    double theta = TWOPI * 0.5 * (raw_alpha1 + raw_alpha2);
    fc_gain =
        evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, expj(theta));
    hf_gain = evaluate(topcoeffs, zplane.numzeros, botcoeffs, zplane.numpoles, -1.0);
    for (i = 0; i <= zplane.numzeros; i++)
        xcoeffs[i] = +(topcoeffs[i].re / botcoeffs[zplane.numpoles].re);
    for (i = 0; i <= zplane.numpoles; i++)
        ycoeffs[i] = -(botcoeffs[i].re / botcoeffs[zplane.numpoles].re);
}

/* compute product of poles or zeros as a polynomial of z */
static void expand(complex pz[], int npz, complex coeffs[]) {
    int i;
    coeffs[0] = 1.0;
    for (i = 0; i < npz; i++)
        coeffs[i + 1] = 0.0;
    for (i = 0; i < npz; i++)
        multin(pz[i], npz, coeffs);
    /* check computed coeffs of z^k are all real */
    for (i = 0; i < npz + 1; i++) {
        if (fabs(coeffs[i].im) > 0.001) {
            MILO_NOTIFY("Filter coefficients are not real");
            return;
        }
    }
}

/* multiply factor (z-w) into coeffs */
static void multin(complex w, int npz, complex coeffs[]) {
    complex nw = -w;
    for (int i = npz; i >= 1; i--)
        coeffs[i] = (nw * coeffs[i]) + coeffs[i - 1];
    coeffs[0] = nw * coeffs[0];
}

static void copyresults(FilterBand band, FILTER *out) {
    switch (band) {
    case kHighpass:
        out->gain = (float)hypot(hf_gain);
        break;
    case kBandpass:
        out->gain = (float)hypot(fc_gain);
        break;
    case kLowpass:
    case kBandstop:
        out->gain = (float)hypot(dc_gain);
        break;
    default:
        break;
    }
    out->gain2 = out->gain * out->gain;
    out->invgain2 = 1.0f / out->gain2;
    int i;
    for (i = 0; i < zplane.numzeros + 1; i++)
        out->xcoeffs[i] = (float)xcoeffs[i];
    for (i = 0; i < zplane.numpoles; i++)
        out->ycoeffs[i] = (float)ycoeffs[i];
    out->numpoles = zplane.numpoles;
    out->numzeros = zplane.numzeros;
}

global void createFilter(
    FilterType type, FilterBand band, uint mask, float alpha1, float alpha2, FILTER *out,
    int ord
) {
    order = ord;
    /* Store both alphas before the polemask test: the target sinks these two
       stores above the branch, which lets alpha2 die immediately. Assigning
       them after the `if` instead forces alpha2 to be kept in a second FPR
       across it (a spurious `fmr f13, f2`). */
    raw_alpha1 = alpha1;
    raw_alpha2 = alpha2;
    if (!(mask & opt_p))
        polemask = ~0;
    /* raw_alpha1, not alpha1: alpha1 is float and raw_alpha1 is double, so
       `raw_alpha1 = alpha1` materializes the widened value in its own register
       (the otherwise-dead `fmr f0, f1` at entry). Re-reading the global here
       consumes that widened value; assigning from the float parameter again
       would emit a second conversion and store f1. */
    if (band != kBandpass && band != kBandstop)
        raw_alpha2 = raw_alpha1;
    if (type == kResonator) { /* resonator */
        if (band == kBandpass) /* bandpass resonator */
            compute_bpres();
        if (band == kBandstop) { /* bandstop resonator (notch) */
            compute_bpres();
            double theta = TWOPI * raw_alpha1;
            complex zz = expj(theta); /* place zeros exactly */
            zplane.zeros[0] = zz;
            zplane.zeros[1] = cconj(zz);
        }
        if (band == kAllpass) /* allpass resonator */
            compute_apres();
    } else {
        /* NB: the z-transform lives INSIDE this else, as in upstream mkfilter.
           The resonator paths above compute zplane directly and must not be
           followed by compute_z_blt()/compute_z_mzt(), which would overwrite
           zplane from a splane those paths never populate. */
        if (type == kProportionalIntegral) { /* proportional-integral */
            applyWarp((mask & (opt_w | opt_z)) == 0);
            splane.poles[0] = 0.0;
            splane.zeros[0] = -TWOPI * warped_alpha1;
            splane.numpoles = splane.numzeros = 1;
        } else {
            compute_s(type);
            applyWarp((mask & (opt_w | opt_z)) == 0);
            normalize(band);
        }
        if (mask & opt_z)
            compute_z_mzt();
        else
            compute_z_blt();
    }
    if (mask & opt_Z)
        add_extra_zero();
    expandpoly();
    copyresults(band, out);
}

#include <cstdlib>
#include <vector>

// Forward declarations for destructors
namespace stlpmtx_std {
    template<typename T, typename Alloc>
    class vector;
}

namespace Synapse {
    namespace DSP {
        class GranularSynth;
        class PitchDetector;
        class PitchCorrectedVoice;
        class Synapse;
    }
}

// Allocator template declarations
namespace stlpmtx_std {
    template<typename T>
    class StlNodeAlloc;
}

// Forward declarations of external functions
extern "C" {
    void merged_OperatorDelete(void *ptr);
    void OnlyReturns(void *ptr);
}

// Destructor for vector types
namespace stlpmtx_std {
    template<typename T, typename Alloc>
    void vector_destructor_M(void *ptr);

    template<typename T, typename Alloc>
    void vector_destructor_PitchCorrectedVoice(void *ptr);

    template<typename T, typename Alloc>
    void vector_vector_destructor(void *ptr);
}

// Allocator deallocate functions
extern "C" {
    void allocate_deallocate_M(void *allocator, void *ptr, int count);
    void allocate_deallocate_PAM(void *allocator, void *ptr, int count);
}

// GranularSynth destructor
namespace Synapse {
    namespace DSP {
        extern void GranularSynth_destructor(void *ptr);
        extern void PitchDetector_destructor(void *ptr);
        extern void vector_PitchCorrectedVoice_destructor(void *ptr);
        extern void vector_vector_M_destructor(void *ptr);
    }
}

namespace Synapse {
    namespace DSP {
        class Synapse {
        public:
            // Member variables at various offsets (inferred from m2c)
            void *unknown[3];  // 0x0
            void *vector1_ptr;  // 0xC
            void *vector1_end;  // 0x10
            // ... more members
            void *vector2_ptr;  // 0x40 - OnlyReturns
            // ... more members
            void *vector3_ptr;  // 0x50 - deallocate
            void *vector3_end;  // 0x54
            void *pitchCorrected;  // 0x5C
            void *granularSynth;  // 0x68
            void *field_0x70;  // 0x70 - merged_OperatorDelete
            void *field_0x74;  // 0x74 - merged_OperatorDelete
            void *pitchDetector;  // 0x28

            // Destructor
            ~Synapse();
        };
    }
}

// Implementation
namespace Synapse {
    namespace DSP {
        Synapse::~Synapse() {
            // Delete field at 0x74
            merged_OperatorDelete(*((void**)((char*)this + 0x74)));

            // Delete field at 0x70
            merged_OperatorDelete(*((void**)((char*)this + 0x70)));

            // GranularSynth destructor at 0x68
            void *granularSynth = *((void**)((char*)this + 0x68));
            if (granularSynth != nullptr) {
                GranularSynth_destructor(granularSynth);
                merged_OperatorDelete(granularSynth);
            }

            // Vector at 0x5C
            vector_PitchCorrectedVoice_destructor((char*)this + 0x5C);

            // Deallocate vector at 0x50
            void *vector_ptr = *((void**)((char*)this + 0x50));
            if (vector_ptr != nullptr) {
                void *end = *((void**)((char*)this + 0x54));
                int count = ((unsigned int)end - (unsigned int)vector_ptr) >> 2;
                allocate_deallocate_PAM((char*)this + 0x50, (char*)this + 0x50 + 8, count);
            }

            // Vector destructor at 0x44
            vector_vector_M_destructor((char*)this + 0x44);

            // OnlyReturns at 0x40
            void *onlyReturns = *((void**)((char*)this + 0x40));
            if (onlyReturns != nullptr) {
                OnlyReturns(onlyReturns);
                merged_OperatorDelete(onlyReturns);
            }

            // PitchDetector at 0x28
            void *pitchDetector = *((void**)((char*)this + 0x28));
            if (pitchDetector != nullptr) {
                PitchDetector_destructor(pitchDetector);
                merged_OperatorDelete(pitchDetector);
            }

            // Deallocate vector at 0x0C
            void *vector_ptr2 = *((void**)((char*)this + 0x0C));
            if (vector_ptr2 != nullptr) {
                void *end2 = *((void**)((char*)this + 0x10));
                int count2 = ((unsigned int)end2 - (unsigned int)vector_ptr2) >> 2;
                allocate_deallocate_M((char*)this + 0x0C, (char*)this + 0x0C + 8, count2);
            }

            // Deallocate vector at 0x0
            void *vector_ptr3 = *((void**)((char*)this + 0x0));
            if (vector_ptr3 != nullptr) {
                void *end3 = *((void**)((char*)this + 0x8));
                int count3 = ((unsigned int)end3 - (unsigned int)vector_ptr3) >> 2;
                allocate_deallocate_M((char*)this, (char*)this + 0x8, count3);
            }
        }
    }
}

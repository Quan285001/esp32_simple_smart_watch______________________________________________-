#include "face_model.h"

// The model is intentionally fixed-capacity. No per-object String allocations
// are required after parsing, and the renderer can reuse it on every frame.

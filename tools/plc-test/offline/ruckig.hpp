// Only declarations needed by the production S-curve class header.
// The standalone target rejects S-curve construction; no Ruckig code is linked.
#pragma once
namespace ruckig {
constexpr unsigned int DynamicDOFs = 0;
template<unsigned int> class Ruckig;
template<unsigned int> class InputParameter;
template<unsigned int> class OutputParameter;
}

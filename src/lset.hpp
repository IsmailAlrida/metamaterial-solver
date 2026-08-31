#pragma once
#include <stdexcept>
#include "mfem.hpp"

namespace App {

class LevelSet {

    public:

        void setActiveDesignDofs(const mfem::Array<int>& active_dofs)
        {
            for (int i = 0; i < active_dofs.Size(); ++i) {
                if (active_dofs[i] < 0 || active_dofs[i] >= design.Size()) {
                    throw std::out_of_range(
                        "Active level-set DOF is outside the design vector.");
                }
            }
            activeDesignDofs = active_dofs;
            enforceDesignConstraints();
        }

        void enforceDesignConstraints()
        {
            design.SetSubVectorComplement(activeDesignDofs, 0.0);
        }

        mfem::Vector design;
        mfem::Vector phi;
        mfem::Array<int> activeDesignDofs;


};


} // namespace App

#pragma once
#include <vector>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>
#include "mfem.hpp"
#include "settings.hpp"

namespace App {

// TODO: Make this a class that extends 


class LevelSet {

    public:

        // Apparenlty the member initilaizer constructs the members with the args passed to it as you see

        explicit LevelSet(mfem::FiniteElementSpace& fes)
        :
        design(fes.GetTrueVSize()),
        phi(&fes)
        {

        }
        
        mfem::Vector design;
        mfem::GridFunction phi;


};


}; // namespace App


// struct LevelSet {
//     int dim; 
//     int nx, ny, nz;
//     std::array<double, 3> origin;
//     std::array<double, 3> spacing; 

//     // the design variable is for the optimizer
//     std::vector<double> design;
//     std::vector<double> phi;

//     LevelSet(int dimension, int x_size, int y_size = 1, int z_size = 1)
//     : 
//     dim(validate_dim(dimension)),
//     nx(validate_extent(x_size)),
//     ny(dim >= 2 ? validate_extent(y_size) : 1),
//     nz(dim == 3 ? validate_extent(z_size) : 1),
//     phi(static_cast<std::size_t>(nx) * ny * nz, double{1.0})
//     {}


//     std::size_t flat_index(int i, int j, int k) const {
//         if (i < 0 || i >= nx || j < 0 || j >= ny || k < 0 || k >= nz) {
//             throw std::out_of_range("LevelSet index out of range!");
//         }

//         return static_cast<std::size_t>(i)
//                 + static_cast<std::size_t>(nx)
//                     * (static_cast<std::size_t>(j)
//                         + static_cast<std::size_t>(ny) * k);
//     }


//     private:
//         static int validate_dim(int d) {
//             if (d < 1 || d > 3) {
//                 throw std::invalid_argument("LevelSet dimension must be 1, 2, or 3!");
//             }
//             return d;
//         }

//         static int validate_extent(int extent) {
//             if (extent <= 0) {
//                 throw std::invalid_argument("LevelSet extents must be positive!");
//             }
//             return extent;
//         }

// };

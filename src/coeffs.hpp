#include "mfem.hpp"
#include <iostream>
#include <string>

class LevelSetScaledCoefficient : public mfem::Coefficient
{
private:
    const mfem::GridFunction &phi;
    double coeff;
    double eps_f;
    bool   active_when_positive;

public:
    LevelSetScaledCoefficient(const mfem::GridFunction &phi_,
                                 double coeff_,
                                 double eps_f_,
                                 bool active_when_positive_
                                )
        : phi(phi_), coeff(coeff_), eps_f(eps_f_), active_when_positive(active_when_positive_)
    {
    }

    double Eval(mfem::ElementTransformation &T,
                const mfem::IntegrationPoint &ip) override
    {
        double phi_value = phi.GetValue(T, ip);

        double alpha_s = active_when_positive ? 
            ((phi_value >= 0.0) ? 1.0 : eps_f):
            ((phi_value <= 0.0) ? 1.0 : eps_f);

        return alpha_s * coeff;
    }
};

class NegatedCoefficient final : public mfem::Coefficient {
public:
    explicit NegatedCoefficient(mfem::Coefficient &source) : source(source) {}

    mfem::real_t Eval(mfem::ElementTransformation &T,
                      const mfem::IntegrationPoint &ip) override {
        return -source.Eval(T, ip);
    }

private:
    Coefficient &source;
};
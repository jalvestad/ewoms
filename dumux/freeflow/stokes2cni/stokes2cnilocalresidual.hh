/*****************************************************************************
 *   Copyright (C) 2010 by Klaus Mosthaf                                     *
 *   Copyright (C) 2008-2009 by Bernd Flemisch, Andreas Lauser               *
 *   Institute of Hydraulic Engineering                                      *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 *
 * \brief Element-wise calculation of the Jacobian matrix for problems
 *        using the non-isothermal compositional stokes box model.
 *
 */
#ifndef DUMUX_STOKES2CNI_LOCAL_RESIDUAL_HH
#define DUMUX_STOKES2CNI_LOCAL_RESIDUAL_HH

#include <dumux/freeflow/stokes2c/stokes2clocalresidual.hh>

#include <dumux/freeflow/stokes2cni/stokes2cnivolumevariables.hh>
#include <dumux/freeflow/stokes2cni/stokes2cnifluxvariables.hh>

namespace Dumux
{
/*!
 * \ingroup Stokes2cniModel
 * \brief Element-wise calculation of the Jacobian matrix for problems
 *        using the non-isothermal compositional stokes box model. This is derived
 *        from the stokes2c box model.
 */
template<class TypeTag>
class Stokes2cniLocalResidual : public Stokes2cLocalResidual<TypeTag>
{
    typedef Stokes2cLocalResidual<TypeTag> ParentType;

    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;

    typedef typename GET_PROP_TYPE(TypeTag, Stokes2cniIndices) Indices;

    enum {
        dim = GridView::dimension,
        numEq = GET_PROP_VALUE(TypeTag, NumEq)
    };
    enum { energyIdx = Indices::energyIdx }; //!< Index of the transport equation

    typedef typename GET_PROP_TYPE(TypeTag, PrimaryVariables) PrimaryVariables;
    typedef typename GET_PROP_TYPE(TypeTag, VolumeVariables) VolumeVariables;
    typedef typename GET_PROP_TYPE(TypeTag, FluxVariables) FluxVariables;
    typedef typename GET_PROP_TYPE(TypeTag, ElementVolumeVariables) ElementVolumeVariables;

    typedef typename GridView::IntersectionIterator IntersectionIterator;

public:
    /*!
     * \brief Constructor. Sets the upwind weight.
     */
    Stokes2cniLocalResidual()
    {
        // retrieve the upwind weight for the mass conservation equations. Use the value
        // specified via the property system as default, and overwrite
        // it by the run-time parameter from the Dune::ParameterTree
        massUpwindWeight_ = GET_PARAM(TypeTag, Scalar, MassUpwindWeight);
    };

    /*!
     * \brief Evaluate the amount the additional quantities to the stokes2c model
     *        (energy equation).
     *
     * The result should be averaged over the volume (e.g. phase mass
     * inside a sub control volume divided by the volume)
     */
    void computeStorage(PrimaryVariables &result, int scvIdx, bool usePrevSol) const
    {
        // compute the storage term for the transport equation
        ParentType::computeStorage(result, scvIdx, usePrevSol);

        // if flag usePrevSol is set, the solution from the previous
        // time step is used, otherwise the current solution is
        // used. The secondary variables are used accordingly.  This
        // is required to compute the derivative of the storage term
        // using the implicit euler method.
        const ElementVolumeVariables &elemDat = usePrevSol ? this->prevVolVars_() : this->curVolVars_();
        const VolumeVariables &vertexDat = elemDat[scvIdx];

        // compute the storage of energy
        result[energyIdx] =
            vertexDat.density() *
            vertexDat.internalEnergy();
    }

    /*!
     * \brief Evaluates the convective energy flux
     * over a face of a subcontrol volume and writes the result in
     * the flux vector.
     *
     * This method is called by compute flux (base class)
     */
    void computeAdvectiveFlux(PrimaryVariables &flux,
                              const FluxVariables &fluxVars) const
    {
        // call computation of the advective fluxes of the stokes model
        // (momentum and mass fluxes)
        ParentType::computeAdvectiveFlux(flux, fluxVars);

        // vertex data of the upstream and the downstream vertices
        const VolumeVariables &up = this->curVolVars_(fluxVars.upstreamIdx());
        const VolumeVariables &dn = this->curVolVars_(fluxVars.downstreamIdx());

        Scalar tmp = fluxVars.normalVelocityAtIP();

        tmp *=  massUpwindWeight_ *         // upwind data
            up.density() * up.enthalpy() +
            (1 - massUpwindWeight_) *     // rest
            dn.density() * dn.enthalpy();

        flux[energyIdx] += tmp;
        Valgrind::CheckDefined(flux[energyIdx]);
    }

    /*!
     * \brief Adds the conductive energy flux to the flux vector over
     *        the face of a sub-control volume.
     */
    void computeDiffusiveFlux(PrimaryVariables &flux,
                              const FluxVariables &fluxVars) const
    {
        // diffusive mass flux
        ParentType::computeDiffusiveFlux(flux, fluxVars);

        // diffusive heat flux
        for (int dimIdx = 0; dimIdx < dim; ++dimIdx)
            flux[energyIdx] -=
                fluxVars.temperatureGradAtIP()[dimIdx] *
                fluxVars.face().normal[dimIdx] *
                fluxVars.heatConductivityAtIP();
    }

    // handle boundary conditions for a single sub-control volume face
    // evaluate one part of the Dirichlet-like conditions for the temperature
    // rest is done in local coupling operator
    void evalCouplingVertex_(const IntersectionIterator &isIt,
                             const int scvIdx,
                             const int boundaryFaceIdx,
                             const FluxVariables& boundaryVars)
    {
        ParentType::evalCouplingVertex_(isIt, scvIdx, boundaryFaceIdx, boundaryVars);
        const VolumeVariables &volVars = this->curVolVars_()[scvIdx];

        if (this->bcTypes_(scvIdx).isCouplingOutflow(energyIdx))
            this->residual_[scvIdx][energyIdx] = volVars.temperature();
    }

protected:
    Scalar massUpwindWeight_;
};

}

#endif
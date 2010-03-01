//////////////////////////////////////////////////////////////////
// (c) Copyright 2008-  by Jeongnim Kim
//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//   National Center for Supercomputing Applications &
//   Materials Computation Center
//   University of Illinois, Urbana-Champaign
//   Urbana, IL 61801
//   e-mail: jnkim@ncsa.uiuc.edu
//
// Supported by
//   National Center for Supercomputing Applications, UIUC
//   Materials Computation Center, UIUC
//////////////////////////////////////////////////////////////////
// -*- C++ -*-
#ifndef QMCPLUSPLUS_DIFFERENTIAL_ONEBODYJASTROW_H
#define QMCPLUSPLUS_DIFFERENTIAL_ONEBODYJASTROW_H
#include "Configuration.h"
#include "QMCWaveFunctions/DiffOrbitalBase.h"
#include "Particle/DistanceTableData.h"
#include "Particle/DistanceTable.h"
#include "ParticleBase/ParticleAttribOps.h"
#include "Utilities/IteratorUtility.h"


namespace qmcplusplus
  {

  /** @ingroup OrbitalComponent
   *  @brief Specialization for two-body Jastrow function using multiple functors
   */
  template<class FT>
  class DiffOneBodySpinJastrowOrbital: public DiffOrbitalBase
    {


      ///number of variables this object handles
      int NumVars;
      ///number of target particles
      int NumPtcls;
      ///reference to the ions
      const ParticleSet& CenterRef;
      ///read-only distance table
      const DistanceTableData* d_table;
      ///variables handled by this orbital
      opt_variables_type myVars;
      ///container for the Jastrow functions  for all the pairs
      Matrix<FT*> Fs;
      ///container for the unique Jastrow functions
      Matrix<int> Fmask;
      vector<int> s_offset;
      vector<int> t_offset;
      vector<pair<int,int> > OffSet;

      Vector<RealType> dLogPsi;
      vector<GradVectorType*> gradLogPsi;
      vector<ValueVectorType*> lapLogPsi;

    public:

      bool SpinPolarized;

      ///constructor
      DiffOneBodySpinJastrowOrbital(const ParticleSet& centers, ParticleSet& els)
          :CenterRef(centers),NumVars(0),SpinPolarized(false)
      {
        NumPtcls=els.getTotalNum();
        d_table=DistanceTable::add(centers,els);
        F.resize(CenterRef.groups(), els.groups());
        for(int i=0; i<F.size(); ++i) F(i)=0;
        Fmask.resize(CenterRef.groups(), els.groups());
        Fmask=-1;
        s_offset.resize(CenterRef.groups()+1,0);
        t_offset.resize(els.groups()+1,0);
        for(int s=0;s<F.rows(); ++s) s_offset[s+1]=centers.last(s);
        for(int t=0;t<F.cols(); ++t) t_offset[t+1]=els.last(t);
      }

      ~DiffOneBodySpinJastrowOrbital()
      {
        delete_iter(gradLogPsi.begin(),gradLogPsi.end());
        delete_iter(lapLogPsi.begin(),lapLogPsi.end());
      }

      /** Add a radial functor for a group
       * @param source_type group index of the center species
       * @param afunc radial functor
       */
      void addFunc(int source_type, FT* afunc, int target_type=-1)
      {
        if(target_g<0)
        {
          SpinPolarized=false;
          int pid=source_g*F.cols();
          for(int ig=0; ig<F.cols(); ++ig)
          {
            F(source_g,ig)=afunc;
            Fmask(source_g,ig)=pid;
          }
        }
        else
        {
          SpinPolarized=true;
          F(source_g,target_g)=afunc;
          Fmask(source_g,target_g)=source_g*F.cols()+target_g;
        }
      }


      ///reset the value of all the unique Two-Body Jastrow functions
      void resetParameters(const opt_variables_type& active)
      {
        for(int i=0; i<F.size(); ++i)
          if(Fmask(i) == i) F(i)->resetParameters(active);
      }

      void checkOutVariables(const opt_variables_type& active)
      {
        myVars.clear();
        for(int i=0; i<F.size(); ++i)
          if(Fmask(i) == i) 
          {
            F(i)->checkOutVariables(active);
            myVars.insertFrom(F(i)->myVars);
          }
        myVars.getIndex(active);
        NumVars=myVars.size();

        if (NumVars && dLogPsi.size()==0)
        {
          dLogPsi.resize(NumVars);
          gradLogPsi.resize(NumVars,0);
          lapLogPsi.resize(NumVars,0);
          for (int i=0; i<NumVars; ++i)
          {
            gradLogPsi[i]=new GradVectorType(NumPtcls);
            lapLogPsi[i]=new ValueVectorType(NumPtcls);
          }
        }
      }

      ///reset the distance table
      void resetTargetParticleSet(ParticleSet& P)
      {
        d_table = DistanceTable::add(CenterRef,P);
      }

      void evaluateDerivatives(ParticleSet& P,
                               const opt_variables_type& active,
                               vector<RealType>& dlogpsi,
                               vector<RealType>& dhpsioverpsi)
      {
        dLogPsi=0.0;
        for (int p=0;p<NumVars; ++p)(*gradLogPsi[p])=0.0;
        for (int p=0;p<NumVars; ++p)(*lapLogPsi[p])=0.0;
        vector<PosType> derivs(NumVars);
        for(int ig=0; ig<F.rows(); ++ig)//species
        {
          for(int iat=s_offset[ig]; iat< s_offset[ig+1]; ++iat)//
          {
            int nn=d_table->M[iat];//starting nn for the iat-th source
            for(int jg=0; jg<F.cols(); ++jg)
            {
              FT* func=F(ig,jg);
              if(func && func->is_optimizable()) 
              {
                int first=func->myVars.Index.front()-myVars.Index[0];
                int last=func->myVars.Index.back()-myVars.Index[0];
                for(int jat=t_offset[jg]; jat< t_offset[jg+1]; ++jat,++nn)
                {
                  std::fill(derivs.begin(),derivs.end(),0.0);
                  if (!func->evaluateDerivatives(d_table->r(nn),derivs)) continue;
                  RealType rinv(d_table->rinv(nn));
                  PosType dr(d_table->dr(nn));
                  for (int p=first, ip=0; p<last; ++p,++ip)
                  {
                    dLogPsi[p] -= derivs[ip][0];
                    RealType dudr(rinv*derivs[ip][1]);
                    (*gradLogPsi[p])[jat] -= dudr*dr;
                    (*lapLogPsi[p])[jat] -= derivs[ip][2]+2.0*dudr;
                  }
                }
              }
              else
              {
                nn+=t_offset[jg+1]-t_offset[jg];
              }
            }//j groups
          }//iat in the ig-th group
        }//ig

        for (int k=0; k<myVars.size(); ++k)
        {
          int kk=myVars.where(k);
          if (kk<0) continue;
          dlogpsi[kk]=dLogPsi[k];
          dhpsioverpsi[kk]=-0.5*Sum(*lapLogPsi[k])-Dot(P.G,*gradLogPsi[k]);
        }
      }

      DiffOrbitalBasePtr makeClone(ParticleSet& tqp) const
        {
          DiffOneBodySpinJastrowOrbital<FT>* j1copy=new DiffOneBodySpinJastrowOrbital<FT>(CenterRef,tqp);
          j1copy->SpinPolarized=SpinPolarized;

          if(SpinPolarized)//full matrix
          {
            for(int sg=0; sg<F.rows(); ++sg)
              for(int tg=0; tg<F.cols(); ++tg)
                j1copy->addFunc(sg,new FT(*F(sg,tg)),tg);
          }
          else//only F(*,0) is created and shared F(*,1..*)
          {
            for(int sg=0; sg<F.rows(); ++sg)
              j1copy->addFunc(sg,new FT(*F(sg,0)),-1);
          }

          //j1copy->OrbitalName=OrbitalName+"_clone";
          j1copy->myVars.clear();
          j1copy->myVars.insertFrom(myVars);
          j1copy->NumVars=NumVars;
          j1copy->NumPtcls=NumPtcls;
          j1copy->dLogPsi.resize(NumVars);
          j1copy->gradLogPsi.resize(NumVars,0);
          j1copy->lapLogPsi.resize(NumVars,0);
          for (int i=0; i<NumVars; ++i)
            {
              j1copy->gradLogPsi[i]=new GradVectorType(NumPtcls);
              j1copy->lapLogPsi[i]=new ValueVectorType(NumPtcls);
            }
          j1copy->OffSet=OffSet;

          return j1copy;
        }


    };
}
#endif
/***************************************************************************
 * $RCSfile$   $Author: jnkim $
 * $Revision: 1761 $   $Date: 2007-02-17 17:11:59 -0600 (Sat, 17 Feb 2007) $
 * $Id: OneBodyJastrowOrbital.h 1761 2007-02-17 23:11:59Z jnkim $
 ***************************************************************************/


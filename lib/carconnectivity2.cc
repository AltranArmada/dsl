#include "carconnectivity2.h"
#include <iostream>
#include "utils.h"
#include <algorithm>


namespace dsl {

using namespace Eigen;
using namespace std;

using Eigen::Vector2d;
using Eigen::Vector3d;
using Eigen::Vector3d;
using Eigen::Matrix3d;
using Eigen::Affine3d;
using std::vector;

typedef Eigen::Matrix<bool, Eigen::Dynamic, Eigen::Dynamic> MatrixXbool;

CarConnectivity2::CarConnectivity2(const CarGrid& grid, CarPrimitiveCfg& cfg)
  :grid(grid){
SetPrimitives(1, cfg);
}

Vector2d xy2wt( double xf,double yf, double u ){
double w, t;
  if(abs(yf)<1e-12){
    w=0;
    t=xf/u;
  }else{
    w = 2*yf/(u*(xf*xf+yf*yf));
    t = atan2(w*xf/u, 1-w*yf/u)/w;
  }
  return Vector2d(w,t);
}

CarConnectivity2::CarConnectivity2(const CarGrid& grid,
                                 const vector<Eigen::Vector3d> &vs,
                                 double dt) : grid(grid), vs(vs), dt(dt)
{
}

CarConnectivity2::CarConnectivity2(const CarGrid& grid,
                                 double dt,
                                 double vx,
                                 double kmax,
                                 int kseg,
                                 bool onlyfwd)
    : grid(grid) {
  SetPrimitives(dt, vx, kmax, kseg, onlyfwd);
}


bool CarConnectivity2::SetPrimitives(double dt, CarPrimitiveCfg& cfg){



  cfg.nl = cfg.nl<2 ? 2: cfg.nl;  //! Make sure the number of different traj length is atleast 2
  cfg.na = cfg.na<3 ? 3 : (cfg.na%2==0 ? cfg.na + 1: cfg.na );// make sure that number of divisions of steering angle
                                                              // is a odd number and at least 3

  double u = 1.0; //The speed can be anything. Using for clarity

  double tmin = cfg.lmin/u;
  double tmax = cfg.lmax/u;
  double wmax = u*cfg.tphioverlmax;
  double rmax = cfg.lmax/cfg.amax;
  double ymax = rmax - rmax*cos(cfg.amax);
  double xmax = cfg.lmax;

  this->dt = dt;

  double del_t = exp(log(tmax/tmin)/(cfg.nl-1));

  double del_w = wmax/(cfg.na-1);

  double sx = grid.cs(1);
  double sy = grid.cs(2);


  //create a grid which can accomodate any primitive starting from any orientation
  uint grid_nhc = ceil(max(ymax/sy, xmax/sx)); // number of half cells
  uint grid_nc  = 2*grid_nhc + 1;
  MatrixXbool grid_seen(grid_nc, grid_nc);


  Affine3d igorg_to_mgcorg = Scaling(Vector3d(1/sx, 1/sy, 1)) *Translation3d(Vector3d(grid_nhc*sx, grid_nhc*sx, 0));


  //Allocate space for the
  vss.resize(grid.gs(0));
  for(size_t i=0;i<vss.size();i++)
    vss[i].reserve(cfg.na*cfg.nl);

  for(int idx_a=0; idx_a < grid.gs(0);idx_a++){
    double th = grid.xlb(0) + grid.cs(0)*(idx_a+0.5);
    for (size_t i = 0, size = grid_seen.size(); i < size; i++)
      *(grid_seen.data()+i) = false;
    Affine3d igorg_to_car = igorg_to_mgcorg * AngleAxisd(th,Vector3d::UnitZ());
    for(size_t idx_t=0 ; idx_t< cfg.nl;idx_t++){
        double t = tmin*pow(del_t, idx_t);
        for(size_t idx_w=0 ; idx_w < cfg.na; idx_w++){
            double w = idx_w*del_w;
            double tpert = t*(1 + 0.2*(cos(40*w)-1)); // t perturbed
           // double tpert = t;
            double apert = w*tpert;                      // angle perturbed
            double lpert = u*tpert;
            if(apert > cfg.amax || lpert > cfg.lmax || lpert < cfg.lmin)
                continue;
            Matrix3d gend; se2_exp(gend,Vector3d(w*tpert, u*tpert, 0));
            Vector3d xyzend(gend(0,2),gend(1,2),0);
            Vector3i idxi = (igorg_to_car * xyzend).cast<int>();
            if((uint)idxi(2)>grid_nc-1 || (uint)idxi(1)>grid_nc-1){
                cout<<"idx out of bounds of megagrid:"<<idxi.transpose()<<endl;
                continue;
            }

            if(grid_seen(idxi(2), idxi(1))==true)
                continue;
            else
              grid_seen(idxi(2), idxi(1))=true;

            Vector3d xyzend_snapped = igorg_to_car.inverse()* (idxi.cast<double>()) ;
            Vector2d wtend= xy2wt(xyzend_snapped(0), xyzend_snapped(1),u);
            double wend = wtend(0); double tend = wtend(1);
            Vector3d vp(wend*tend, u*tend,0);
            Vector3d vn(-wend*tend, u*tend,0);

            if(abs(wend)<1e-10){
                vss[idx_a].push_back(vp);
            }else{
              vss[idx_a].push_back(vp);
              vss[idx_a].push_back(vn);
            }
        }
    }
  }


  return true;
}

bool CarConnectivity2::SetPrimitives(double dt, double vx, double kmax, int kseg, bool onlyfwd) {
  if (dt <= 0)
    return false;

  kseg = kseg < 1 ? 1 : kseg;

  this->dt = dt;

  vs.clear();

  for (int i = 0; i <= kseg; i++) {
    double k = i*kmax/kseg;
    double w = vx*k;
    vs.push_back(Vector3d(w, vx, 0));
    vs.push_back(Vector3d(-w, vx, 0));
    if (!onlyfwd) {
      vs.push_back(Vector3d(w, -vx, 0));
      vs.push_back(Vector3d(-w, -vx, 0));
    }
  }

  return true;
}

static Vector2d position(const Matrix3d &g) {
  return Vector2d(g(0,2), g(1,2));
}

bool CarConnectivity2::Flow(std::tuple< SE2Cell*, SE2Path, double>& pathTuple,
                           const Matrix3d& g0,
                           const Vector3d& v) const {

  double d = fabs(v[1]); // total distance along curve
  double s = 2 * grid.cs[1]; // set step-size to 2*side-length

  Matrix3d g;
  Vector3d q;

  SE2Path& path = std::get<1>(pathTuple);
  path.clear();
  SE2Cell *to = nullptr;
  
  // might be better to generate it backwards to more efficiently handle
  // obstacles
  //  for (double a = d; a > 0; a -= s) {
  double eps = 1e-10;
  for (double a = s; a <= d + eps; a += s) {
    Matrix3d dg;
    se2_exp(dg, (a / d) * v);
    g = g0 * dg;
    se2_g2q(q, g);
    
    to = grid.Get(q);
    if (!to) {
      return false;
    }

    path.push_back(g);
  }

  if (!to)
    return false;

  std::get<0>(pathTuple) = to;

  // add distance + mismatch
  std::get<2>(pathTuple) = d + (position(to->data) - position(g)).norm();;
  
  return true; 
}

bool CarConnectivity2::
    operator()(const SE2Cell& from,
               std::vector< std::tuple<SE2Cell*, SE2Path, double> >& paths,
               bool fwd) const {
  Matrix3d g0;
  se2_q2g(g0, from.c);

  const vector<Vector3d>* p_vstemp;
  int idx_a = grid.Index(from.c,0);
  if(!vss.empty())
    p_vstemp = &(vss[idx_a]);
  else
    p_vstemp = &vs;

  paths.clear();
  //  vector< Vector3d >::const_iterator it;
  for (auto&& s : *p_vstemp) {
    // reverse time if fwd=false
    std::tuple<SE2Cell*, SE2Path, double> pathTuple;
    if (!Flow(pathTuple, g0, (fwd ? dt : -dt) * s))
      continue;

    assert(std::get<0>(pathTuple));
    
    
    // the path will now end inside the last cell but not exactly at the center,
    // which is a good enough
    // approximation if the cells are small

    // For exact trajectory generation to the center, we need to use more
    // complex inverse kinematics
    // which can be accomplished by uncommenting the following
    //    GenTraj(path.data, g0, path.cells.back().data, w,vx, vx, w,vx, dt/5);
    
    if (!std::get<1>(pathTuple).size())
      continue;

    // overwrite cost 
    //    path.cost = cost; //.Real(path.cells.front(), path.cells.back());

    //    path.fwd = fwd;
    paths.push_back(pathTuple);
  }
  return true;
}

}
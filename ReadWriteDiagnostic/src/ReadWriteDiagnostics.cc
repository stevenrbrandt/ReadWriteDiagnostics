#include <set>
#include <vector>
#include <map>
#include <iostream>
#include <cstring>
#include <string>
#include <ScheduleWrapper.hh>
#include <cctk_Sync.h>
#include <cctk_Arguments.h>
#include <cctk_Parameters.h>
#include <sstream>
#include <stdlib.h>

#include "PreSync.h"

extern "C" void CCTK_Checked_called(), CCTK_Checked_reset();
extern "C" int CCTK_Checked_get();

namespace Read_Write_Diagnostics {
  static unsigned short internet_checksum(void const *restrict const addr,
                                          size_t const len);
  struct VarName {
    char *name;
    VarName(int vi) : name(CCTK_FullName(vi)) {}
    ~VarName() { free(name); }
    operator const char *() const { return name; }
  };
  extern "C" int GetRefinementLevel(const cGH*);

#if 0
//  #define WH_EVERYWHERE 0x11
//  #define WH_INTERIOR   0x01
//  #define WH_EXTERIOR   0x10
#define WH_EVERYWHERE          0x7
#define WH_INTERIOR            0x4
#define WH_BOUNDARY            0x2 /* Describes B+G cells */
#define WH_GHOSTS              0x1
#define WH_NOWHERE             0x0
#define WH_EXTERIOR            0x3
#endif
  void traceVars(const cGH *cctkGH) {;
    const char *ixs = getenv("RDWR_DEBUG_INDEXES");
    int xv=0,yv=0,zv=0;
    if(ixs != 0) {
      std::istringstream cns{ixs};
      cns >> xv >> yv >> zv;
    }

    const char *dbv = getenv("RDWR_DEBUG_VARS");
    if(dbv == 0)
      return;
  
    std::istringstream ins{dbv};
    while(ins.good()) {
      std::string vname;
      ins >> vname;
      //const char *vname = "ADMBASE::gxx";
      int vi_ = CCTK_VarIndex(vname.c_str());
      assert(vi_ >= 0);
      int cc = CCTK_GFINDEX3D(cctkGH,xv,yv,zv);
      CCTK_REAL *ptr = (CCTK_REAL*)CCTK_VarDataPtrI(cctkGH,0,vi_);
      int wh = Carpet_GetValidRegion(vi_,0);
      std::string whs;
      if((wh & WH_INTERIOR) != 0) whs += "I";
      if((wh & WH_BOUNDARY) != 0) whs += "B";
      if((wh & WH_GHOSTS) != 0) whs += "G";
      if(ptr == 0)
        std::cout << "   " << vname << " := ??? (" << whs << ")" << std::endl;
      else {
        if(xv == -1 && yv == -1 && zv == -1) {
          unsigned int cksum = internet_checksum(ptr, cctkGH->cctk_ash[0]*cctkGH->cctk_ash[1]*cctkGH->cctk_ash[2]*sizeof(CCTK_REAL));
          std::cout << "   " << vname << " := " << cksum << " (" << whs << ")" << std::endl;
        } else {
          std::cout << "   " << vname << " := " << ptr[cc] << " (" << whs << ")" << std::endl;
        }
      }
    }
  }

  struct cksum_t {
    unsigned long in, out;
    cksum_t() : in(0), out(0) {}
  };

  std::map<int,cksum_t> cksums;
  std::set<std::string> messages;

  inline bool operator==(const cksum_t& c1,const cksum_t& c2) {
    return c1.in == c2.in && c1.out == c2.out;
  }
  inline bool operator!=(const cksum_t& c1,const cksum_t& c2) {
    return c1.in != c2.in || c1.out != c2.out;
  }
  inline std::ostream& operator<<(std::ostream& out,const cksum_t& c) {
    return out << std::hex << c.in << ":" << c.out << std::dec;
  }
  inline const char *wh_name(int n) {
    if(n == WH_EVERYWHERE) 
      return "everywhere";
    if(n == WH_INTERIOR)
      return "interior";
    if(n == WH_EXTERIOR)
      return "exterior";
    if(n == WH_GHOSTS)
      return "ghosts";
    if(n == WH_BOUNDARY)
      return "boundary";
    if(n == WH_NOWHERE)
      return "nowhere";
    if(n == (WH_INTERIOR|WH_BOUNDARY))
      return "interior+boundary";
    if(n == (WH_INTERIOR|WH_GHOSTS))
      return "interior+ghosts";
    return "?";
  }
  inline void tolower(std::string& s) {
    for(auto si=s.begin();si != s.end();++si) {
      if(*si >= 'A' && *si <= 'Z') {
        *si = *si + 'a' - 'A';
      }
    }
  }

  // Writes, Reads, Syncs
  std::map<std::string,std::map<int,int> > wclauses, rclauses;
  std::map<std::string,std::set<int> > syncs;
  std::map<int,std::map<int,std::string> > track_syncs;

  std::string routine;

  void compute_clauses(const int num_strings,const char **strings,std::map<int,int>& routine_m) {
    for(int i=0;i< num_strings; ++i) {

      // Parse the where clause (if any)
      const char *clause = strings[i];
      const char *openp  = strchr(clause,'(');
      const char *closep = strchr(clause,')');
      const char *end_impl = strchr(clause,':');
      if(end_impl == 0) {
        std::cerr << "bad str=" << strings[i] << std::endl;
      }
      assert(end_impl != 0);
      std::string where, str;
      int where_val=0;
      if(openp != 0) {
        str.assign(clause,openp-clause);
        where.assign(openp+1,closep-openp-1);
        tolower(where);
        std::string imp(clause,end_impl-clause);
        tolower(imp);

        if(where == "everywhere") {
          where_val = WH_EVERYWHERE;
        } else if(where == "interior") {
          where_val = WH_INTERIOR;
        } else if(where == "exterior") {
          where_val = WH_EXTERIOR;
        } else if(where == "boundary") {
          where_val = WH_EXTERIOR;
        } else {
          std::cout << "error in where clause for " << str << "=" << where << std::endl;
          assert(false);
        }
      } else {
        str = clause;
        where_val = WH_EVERYWHERE;
      }

      int vi = CCTK_VarIndex(str.c_str());
      if(vi >= 0) {
        routine_m[vi] = where_val;
      } else {
        // If looking up a specific variable failed, then
        // we lookup everything on the group.
        int gi = CCTK_GroupIndex(str.c_str());
        if(gi >= 0) {
          int i0 = CCTK_FirstVarIndexI(gi);
          int iN = i0+CCTK_NumVarsInGroupI(gi);
          for(vi=i0;vi<iN;vi++) {
            routine_m[vi] = where_val;
          }
        } else {
          std::cerr << "error: Could not find (" << str << ")" << std::endl;
        }
      }
    }
  }

  void init_MoL() {
    // Special code for MoL
    bool init = true;
    if(!init) return;
    std::string add = "MoL::MoL_Add";
    std::map<int,int>& writes_add = wclauses[add];
    std::string copy = "MoL::MoL_InitialCopy";
    std::map<int,int>& writes_copy = wclauses[copy];
    std::string rhs = "MoL::MoL_InitRHS";
    std::map<int,int>& writes_rhs = wclauses[rhs];
    if(routine != add && routine != copy && routine != rhs)
      return;
    int nv = CCTK_NumVars();
    for(int vi=0;vi<nv;vi++) {
      int type = CCTK_GroupTypeFromVarI(vi);
      if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(CCTK_REAL)) {
        int rhsi = CCTK_IsFunctionAliased("MoLQueryEvolvedRHS") ? MoLQueryEvolvedRHS(vi) : -1;
        if(rhsi >= 0) {
          writes_add[vi] = WH_INTERIOR;
          writes_copy[vi] = WH_INTERIOR;
          if(rhsi >= 0)
            writes_rhs[rhsi] = WH_INTERIOR;
          init = false;
        }
      }
    }
  }

  void init_function(const cFunctionData *attribute) {
    if(wclauses.find(routine) != wclauses.end())
      return;

    std::map<int,int>& writes_m = wclauses[routine];
    compute_clauses(
      attribute->n_WritesClauses,
      attribute->WritesClauses,
      writes_m);
    std::map<int,int>& reads_m = rclauses[routine];
    compute_clauses(
      attribute->n_ReadsClauses,
      attribute->ReadsClauses,
      reads_m);

    std::set<int>& syncs_s = syncs[routine];
    for(int i=0;i<attribute->n_SyncGroups;i++) {
      int gi = attribute->SyncGroups[i];
      int i0 = CCTK_FirstVarIndexI(gi);
      int iN = i0+CCTK_NumVarsInGroupI(gi);
      for(int vi=i0;vi<iN;vi++) {
        syncs_s.insert(vi);
      }
    }
  }

  // routine -> var_index -> where_val
  std::map<std::string,std::map<int,int> > observed_writes;
  // Compare observed writes to what is specified in schedule.ccl
  void wclause_diagnostic() {
    for(auto it=observed_writes.begin();it != observed_writes.end(); ++it) {
      const std::string& routine = it->first;
      for(auto v=it->second.begin();v != it->second.end();++v) {
        int vi = v->first;
        //std::string imp = CCTK_ImpFromVarI(vi);
        auto wfind = wclauses.find(routine);
        std::ostringstream msg;
        if(wfind == wclauses.end()) {
          msg << "error: " << routine << "() has no write clauses";
        } else {
          auto vfind = wfind->second.find(vi);
          VarName vn(vi);
          if(vfind == wfind->second.end()) {
            int where = Carpet_GetValidRegion(vi,0);
            int missing = v->second & ~where;
            missing &= ~WH_GHOSTS;
            if(missing != 0) {
              msg << "error: Routine " << routine << "() is missing WRITES: "
                << vn << "(" << wh_name(missing) << ") ";
            }
            /*
            msg << "id=" << vi << " ";
            msg << "has=(";
            for(auto m=wfind->second.begin();m != wfind->second.end();++m) {
              msg << m->first << " ";
            }
            msg << ")";
            */
          } else if(v->second != vfind->second) {
            msg << "error: Routine " << routine << "() has "
              << "incorrect region for region of "
              << "writes clause for " << vn << ": " 
              << " schedule=" << wh_name(vfind->second)
              << " observed=" << wh_name(v->second);
          }
          std::string smsg = msg.str();
          if(smsg.size() > 0) {
            if(messages.find(smsg) == messages.end()) {
              messages.insert(smsg);
              std::cout << smsg << std::endl;
            }
          }
        }
      }
    }
  }

  cksum_t compute_cksum(const cGH *cctkGH,unsigned long *ldata,int vi) {
    const int *cctk_nghostzones = cctkGH->cctk_nghostzones;
    const int *cctk_lsh = cctkGH->cctk_lsh;
    const int bytes = sizeof(unsigned long);
    cksum_t c;
    #if 0
    // When is the data supposed to become valid?
    if (CCTK_IsFunctionAliased("Accelerator_RequireValidData")) {
      bool on_device = 0;
      int rl = GetRefinementLevel(cctkGH);
      int tl = 0;
      Accelerator_RequireValidData(cctkGH, &vi, &rl, &tl, 1, on_device);
    }
    #endif
    for(int k=0;k<cctk_lsh[2];k++) {
      const bool inz = (cctk_nghostzones[2] <= k && k < cctk_lsh[2]-cctk_nghostzones[2]);
      for(int j=0;j<cctk_lsh[1];j++) {
        const bool iny = (cctk_nghostzones[1] <= j && j < cctk_lsh[1]-cctk_nghostzones[1]);
        for(int i=0;i<cctk_lsh[0];i++) {
          const bool inx = (cctk_nghostzones[0] <= i && i < cctk_lsh[0]-cctk_nghostzones[0]);
          const int cc = CCTK_GFINDEX3D(cctkGH,i,j,k);
          const int n = (cc % bytes);
          const int m = bytes-n;
          unsigned long lg = (ldata[cc] << (bytes*n))|(ldata[cc] >> (bytes*m));
          if(inx && iny && inz)
            c.in ^= lg;
          else
            c.out ^= lg;
        }
      }
    }
    return c;
  }

  static unsigned short internet_checksum(void const *restrict const addr,
                                          size_t const len) {
    unsigned long chk = 0;
#pragma omp parallel for reduction(+ : chk)
    for (ptrdiff_t i = 0; i < (ptrdiff_t)len; i += 2) {
      unsigned long const lb = ((unsigned char const *)addr)[i];
      unsigned long const ub = ((unsigned char const *)addr)[i + 1];
      chk += lb + (ub << 8);
    }
    if (len % 1) {
      unsigned long const lb = ((unsigned char const *)addr)[len - 1];
      chk += lb;
    }
    while (chk >> 16) {
      chk = (chk & 0xffffUL) + (chk >> 16);
    }
    return ~chk;
  }

  extern "C" int RDWR_pre_call(const cGH *arg1,void *arg2,const cFunctionData *arg3,void *arg4);
  int RDWR_pre_call(const cGH *arg1,void *arg2,const cFunctionData *arg3,void *arg4)
  {
    CCTK_Checked_reset();
    const cGH *cctkGH = (const cGH *)arg1;

    const cFunctionData *attribute = (const cFunctionData *)arg3;
    std::cout << "/== " << attribute->thorn << "::" << attribute->routine << " it=" << cctkGH->cctk_iteration << " reffact=" << cctkGH->cctk_timefac << std::endl;

    if(GetMap(cctkGH) < 0) {
      CCTK_Checked_called();
      return 0;
    }

    init_MoL();

    routine = attribute->thorn;
    routine += "::";
    routine += attribute->routine;

    init_function(attribute);

    int comp =  GetRefinementLevel(cctkGH);

    traceVars(cctkGH);
    #if 0
    std::istringstream cns{getenv("DEBUG_INDEXES")};
    int xv,yv,zv;
    cns >> xv >> yv >> zv;
  
    std::istringstream ins{getenv("DEBUG_VARS")};
    while(ins.good()) {
      std::string vname;
      ins >> vname;
      //const char *vname = "ADMBASE::gxx";
      int vi_ = CCTK_VarIndex(vname.c_str());
      assert(vi_ >= 0);
      int cc = CCTK_GFINDEX3D(cctkGH,xv,yv,zv);
      CCTK_REAL *ptr = (CCTK_REAL*)CCTK_VarDataPtrI(cctkGH,0,vi_);
      int wh = Carpet_GetValidRegion(vi_,0);
      std::string whs;
      if((wh & WH_INTERIOR) != 0) whs += "I";
      if((wh & WH_BOUNDARY) != 0) whs += "B";
      if((wh & WH_GHOSTS) != 0) whs += "G";
      if(ptr == 0)
        std::cout << "   " << vname << " := ??? (" << whs << ")" << std::endl;
      else
        std::cout << "   " << vname << " := " << ptr[cc] << " (" << whs << ")" << std::endl;
    }
    #endif

    std::set<int> variables_to_check;
    std::map<int,int>& reads_m = rclauses[routine];
    std::set<int> syncs_s = syncs[routine];
    for(auto i=reads_m.begin();i != reads_m.end();++i) {
      if(i->second == (WH_INTERIOR|WH_EXTERIOR)) {
        std::string r = track_syncs[comp][i->first];
        if(r != "") {
          std::ostringstream msg;
          VarName vn(i->first);
          msg << "error: in routine " << routine << ". Variable " << vn <<
            " (group " << CCTK_GroupNameFromVarI(i->first) << ") needs to be synced by " << r;
          messages.insert(msg.str());

          // Fix syncs
          #if 0
          int gi = CCTK_GroupIndexFromVarI(i->first);
          CCTK_SyncGroupsI(cctkGH,1,&gi);
          track_syncs[comp][i->first]="";
          #endif
        }
      }
      variables_to_check.insert(i->first);
    }
    std::map<int,int>& writes_m = wclauses[routine];
    for(auto i=writes_m.begin();i != writes_m.end();++i) {
      if(i->second == WH_INTERIOR) {
        track_syncs[comp][i->first]=routine;
      } else if(i->second == (WH_INTERIOR|WH_EXTERIOR)) {
        track_syncs[comp][i->first]="";
      }
      variables_to_check.insert(i->first);
    }
    for(auto vp = syncs_s.begin();vp != syncs_s.end();++vp) {
      std::string r = track_syncs[comp][*vp];
      if(r == "") {
        std::ostringstream msg;
        VarName vn(*vp);
        msg << "warning: Variable " << vn <<
          " (group " << CCTK_GroupNameFromVarI(*vp) << ") does not need to be synced by " << routine;
        messages.insert(msg.str());
      } else {
        track_syncs[comp][*vp] = "";
      }
    }
    // No read-write clauses. Check everything.
    if(variables_to_check.size()==0) {
      for(int vi=0;vi < CCTK_NumVars();vi++) {
        variables_to_check.insert(vi);
      }
    }

    for(auto i = variables_to_check.begin();i != variables_to_check.end();++i) {
       int vi = *i;
       void *data = CCTK_VarDataPtrI(cctkGH,0,vi);
       if(data == 0) continue;
       int type = CCTK_GroupTypeFromVarI(vi);
       if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(CCTK_REAL)) {
       unsigned long *ldata = (unsigned long*)data;
       cksum_t c = compute_cksum(cctkGH,ldata,vi);
       cksums[vi] = c;
      }
    }
    return 0;
  }

  extern "C" int RDWR_post_call(const cGH *arg1,void *arg2,const cFunctionData * arge,void *arg4);
  int RDWR_post_call(const cGH *arg1,void *arg2,const cFunctionData * arg3,void *arg4)
  {
    const cGH *cctkGH = (const cGH *)arg1;
    const cFunctionData *attribute = (const cFunctionData *)arg3;
    if(CCTK_Checked_get() == 0) {
      std::cout << "xxxy: No check called for " << attribute->thorn << "::" << attribute->routine << "\n";
    }
    CCTK_Checked_reset();

    std::set<int> variables_to_check;
    std::map<int,int>& reads_m = rclauses[routine];
    for(auto i=reads_m.begin();i != reads_m.end();++i) {
      variables_to_check.insert(i->first);
    }
    std::map<int,int>& writes_m = wclauses[routine];
    for(auto i=writes_m.begin();i != writes_m.end();++i) {
      variables_to_check.insert(i->first);
    }
    // No read-write clause. Check everything.
    if(variables_to_check.size()==0) {
      for(int vi=0;vi < CCTK_NumVars();vi++) {
        variables_to_check.insert(vi);
      }
    }

    for(auto i = variables_to_check.begin();i != variables_to_check.end();++i) {
       int vi = *i;
       void *data = CCTK_VarDataPtrI(cctkGH,0,vi);
       if(data == 0) continue;
       int type = CCTK_GroupTypeFromVarI(vi);
       if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(CCTK_REAL)) {
       unsigned long *ldata = (unsigned long*)data;
       cksum_t c = compute_cksum(cctkGH,ldata,vi);
       cksum_t cn = cksums[vi];
        if(cn != c) {
          int where=0;
          if(cn.out != c.out)
            where |= WH_EXTERIOR;
          if(cn.in != c.in)
            where |= WH_INTERIOR;
          observed_writes[routine][vi] |= where;
        }
      }
    }
    wclause_diagnostic();
  
  
    traceVars(cctkGH);
    #if 0
    std::istringstream cns{getenv("DEBUG_INDEXES")};
    int xv,yv,zv;
    cns >> xv >> yv >> zv;

    std::istringstream ins{getenv("DEBUG_VARS")};
    while(ins.good()) {
      std::string vname;
      ins >> vname;
      //const char *vname = "ADMBASE::gxx";
      int vi_ = CCTK_VarIndex(vname.c_str());
      if(vi_ < 0) std::cout << "Invalid(" << vname << ")" << std::endl;
      assert(vi_ >= 0);
      int cc = CCTK_GFINDEX3D(cctkGH,xv,yv,zv);
      CCTK_REAL *ptr = (CCTK_REAL*)CCTK_VarDataPtrI(cctkGH,0,vi_);
      int wh = Carpet_GetValidRegion(vi_,0);
      std::string whs;
      if((wh & WH_INTERIOR) != 0) whs += "I";
      if((wh & WH_BOUNDARY) != 0) whs += "B";
      if((wh & WH_GHOSTS) != 0) whs += "G";
      if(ptr == 0)
        std::cout << "   " << vname << " := ??? (" << whs << ")" << std::endl;
      else
        std::cout << "   " << vname << " := " << ptr[cc] << " (" << whs << ")" << std::endl;
    }
    #endif

    std::cout << "\\== " << attribute->thorn << "::" << attribute->routine << std::endl;
    return 0;
  }

  extern "C" void *RDWR_VarDataPtrI(const cGH *gh,int tl,int vi) {
    bool found = false;
    std::map<int,int>& reads_m = rclauses[routine];
    //int read_mask = 0;
    if(reads_m.find(vi) == reads_m.end()) {
      std::map<int,int>& writes_m = wclauses[routine];
      if(writes_m.find(vi) == writes_m.end()) {
        ; // not found in either reads or writes
      } else {
        found = true;
      }
    } else {
      //read_mask = reads_m[vi];
      found = true;
    }
    int type = CCTK_GroupTypeFromVarI(vi);
    if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(CCTK_REAL)) {
      ; // we only check grid functions of this type
    } else {
      found = true;
    }
    if(found) {
      return (CCTK_REAL*)CCTK_VarDataPtrI(gh,tl,vi);
    } else {
      return 0;
    }
  }

  extern "C" int RDWR_ShowDiagnostics(void) {
    std::cerr << "RDWR Diagnostics:" << std::endl;
    for(auto i=messages.begin();i != messages.end();++i) {
      std::cerr << *i << std::endl;
    }
    return 0;
  }

  extern "C" int RDWR_AddDiagnosticCalls(void) {
    Carpet::Carpet_RegisterScheduleWrapper((Carpet::func)RDWR_pre_call,(Carpet::func)RDWR_post_call);
    std::cout << "Hooks added" << std::endl;
    return 0;
  }

  int is_white(char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
  }

  void fill_vec(const char *in,std::vector<std::string>& vec)
  {
    int n = strlen(in);
    int i = 0;
    std::string out;
    while(i<n) {
      out = "";
      while(i < n && is_white(in[i])) {
        i++;
      }
      while(i < n && !is_white(in[i])) {
        out.push_back(in[i]);
        i++;
      }
      if(out.size() > 0) {
        vec.push_back(out);
      }
    }
  }

  extern "C" int RDWR_ZeroInit_Storage(CCTK_ARGUMENTS) {
    DECLARE_CCTK_ARGUMENTS;
    DECLARE_CCTK_PARAMETERS;
    std::vector<std::string> vec{};
    fill_vec(zero_init,vec);
    for(std::string out : vec)
    {
      int var = CCTK_VarIndex(out.c_str());
      if(var < 0) {
        std::cout << "Zero_init skips (" << out << ") no such variable.\n";
        continue;
      }
      int group = CCTK_GroupIndexFromVarI(var);
      if(group >= 0) {
          std::cout << "Turn on group storage\n";
          CCTK_EnableGroupStorageI(cctkGH,group);
      }
    }
    return 0;
  } // end

  extern "C" int RDWR_ZeroInit(CCTK_ARGUMENTS) {
    DECLARE_CCTK_ARGUMENTS;
    DECLARE_CCTK_PARAMETERS;
    std::vector<std::string> vec{};
    fill_vec(zero_init,vec);
    for(std::string out : vec)
    {
      int var = CCTK_VarIndex(out.c_str());
      if(var < 0) {
        std::cout << "Zero_init skips " << out << " no such variable.\n";
        continue;
      }
      void *data = CCTK_VarDataPtrI(cctkGH,0,var);
      if(data == nullptr) {
        int group = CCTK_GroupIndexFromVarI(var);
        if(group >= 0) {
          std::cout << "Turn on group storage\n";
          CCTK_EnableGroupStorageI(cctkGH,group);
        }
      }
      data = CCTK_VarDataPtrI(cctkGH,0,var);
      if(data == 0) {
        std::cout << "Zero_init skips " << out << " nullptr.\n";
        continue;
      }
      int type = CCTK_GroupTypeFromVarI(var);
      if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(var)) == sizeof(CCTK_REAL)) {
        CCTK_REAL *rdata = (CCTK_REAL *)data;
        for(int k=0;k<cctk_lsh[2];k++) {
          for(int j=0;j<cctk_lsh[1];j++) {
            for(int i=0;i<cctk_lsh[0];i++) {
              int cc = CCTK_GFINDEX3D(cctkGH,i,j,k);
              rdata[cc] = i;
            }
          }
        }
        Carpet_SetValidRegion(var,0,WH_EVERYWHERE);
        std::cout << "Zero_init of " << out << " to Everywhere\n";
      }
    }
    return 0;
  } // end

}


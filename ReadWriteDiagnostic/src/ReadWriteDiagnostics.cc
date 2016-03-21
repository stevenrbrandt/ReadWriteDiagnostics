#include <set>
#include <vector>
#include <map>
#include <iostream>
#include <cstring>
#include <string>
#include <ScheduleWrapper.hh>

namespace Read_Write_Diagnostics {

  #define WH_EVERYWHERE 0x11
  #define WH_INTERIOR   0x01
  #define WH_EXTERIOR   0x10

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
  std::map<std::string,std::set<int>> syncs;

  void compute_clauses(const int num_strings,const char **strings,std::map<int,int>& routine_m) {
    for(int i=0;i< num_strings; ++i) {

      // Parse the where clause (if any)
      const char *clause = strings[i];
      const char *openp  = strchr(clause,'(');
      const char *closep = strchr(clause,')');
      const char *end_impl = strchr(clause,':');
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
        where_val = WH_EVERYWHERE;
      }

      int vi = CCTK_VarIndex(str.c_str());
      if(vi >= 0) {
        routine_m[vi] = where_val;
        std::cout << " FOUND WRITE FOR " << CCTK_VarName(vi) << "=" << where << std::endl;
      } else {
        // If looking up a specific variable failed, then
        // we lookup everything on the group.
        int gi = CCTK_GroupIndex(str.c_str());
        std::cout << " FOUND WRITE FOR " << CCTK_GroupName(gi) << "=" << where << std::endl;
        if(gi >= 0) {
          int i0 = CCTK_FirstVarIndexI(gi);
          int iN = i0+CCTK_NumVarsInGroupI(gi);
          for(vi=i0;vi<iN;vi++) {
            routine_m[vi] = where_val;
          }
        } else {
          std::cout << "error: Could not find (" << str << ")" << std::endl;
        }
      }
    }
  }

  std::string routine;

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
        std::string imp = CCTK_ImpFromVarI(vi);
        auto wfind = wclauses.find(routine);
        std::ostringstream msg;
        if(wfind == wclauses.end()) {
          msg << "error: " << routine << "() has no write clauses";
        } else {
          auto vfind = wfind->second.find(vi);
          std::string vname = CCTK_VarName(vi);
          if(vfind == wfind->second.end()) {
            msg << "error: Routine " << routine << "() is missing WRITES: "
              << imp << "::" << vname << "(" << wh_name(v->second) << ")";
          } else if(v->second != vfind->second) {
            msg << "error: Routine " << routine << "() has "
              << "incorrect region for region of "
              << "writes clause for " << imp << "::" << vname << ": " 
              << " schedule=" << wh_name(vfind->second)
              << " observed=" << wh_name(v->second);
          }
          std::string smsg = msg.str();
          if(smsg.size() > 0) {
            messages.insert(smsg);
          }
        }
      }
    }
  }

  cksum_t compute_cksum(const cGH *cctkGH,unsigned long *ldata) {
    const int *cctk_nghostzones = cctkGH->cctk_nghostzones;
    const int *cctk_lsh = cctkGH->cctk_lsh;
    const int bytes = sizeof(unsigned long);
    cksum_t c;
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

  //int pre_call(const cGH *cctkGH,void *func,const cFunctionData *attribute,call_data *cd_) {
  int pre_call(const void *arg1,void *arg2,void *arg3,void *arg4) {

    const cGH *cctkGH = (const cGH *)arg1;
    const cFunctionData *attribute = (const cFunctionData *)arg3;

    routine = attribute->thorn;
    routine += "::";
    routine += attribute->routine;

    init_function(attribute);

    std::set<int> variables_to_check;
    std::map<int,int>& reads_m = rclauses[routine];
    for(auto i=reads_m.begin();i != reads_m.end();++i) {
      variables_to_check.insert(i->first);
    }
    std::map<int,int>& writes_m = rclauses[routine];
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
       if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(long)) {
       unsigned long *ldata = (unsigned long*)data;
       cksum_t c = compute_cksum(cctkGH,ldata);
       cksums[vi] = c;
      }
    }
    return 0;
  }

  //int post_call(const cGH *cctkGH,void *func,const cFunctionData * attribute,call_data *cd_) {
  int post_call(const void *arg1,void *arg2,void *arg3,void *arg4) {
    const cGH *cctkGH = (const cGH *)arg1;
    //const cFunctionData *attribute = (const cFunctionData *)arg3;

    std::set<int> variables_to_check;
    std::map<int,int>& reads_m = rclauses[routine];
    for(auto i=reads_m.begin();i != reads_m.end();++i) {
      variables_to_check.insert(i->first);
    }
    std::map<int,int>& writes_m = rclauses[routine];
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
       if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(long)) {
       unsigned long *ldata = (unsigned long*)data;
       cksum_t c = compute_cksum(cctkGH,ldata);
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
    return 0;
  }

  extern "C" void *RDWR_VarDataPtrI(const cGH *gh,int tl,int vi) {
    bool found = false;
    std::map<int,int>& reads_m = rclauses[routine];
    if(reads_m.find(vi) == reads_m.end()) {
      std::map<int,int>& writes_m = wclauses[routine];
      if(writes_m.find(vi) == writes_m.end()) {
        ; // not found in either reads or writes
      } else {
        found = true;
      }
    } else {
      found = true;
    }
    int type = CCTK_GroupTypeFromVarI(vi);
    if(type == CCTK_GF && CCTK_VarTypeSize(CCTK_VarTypeI(vi)) == sizeof(long)) {
      ; // we only check grid functions of this type
    } else {
      found = true;
    }
    if(found) {
      return CCTK_VarDataPtrI(gh,tl,vi);
    } else {
      return 0;
    }
  }

  extern "C" int RDWR_ShowDiagnostics() {
    std::cout << "RDWR Diagnostics:" << std::endl;
    for(auto i=messages.begin();i != messages.end();++i) {
      std::cout << *i << std::endl;
    }
    return 0;
  }

  extern "C" int RDWR_AddDiagnosticCalls() {
    //auto *cd = new call_data;
    //AddPreCallFunction((hook_function)pre_call,cd);
    //AddPostCallFunction((hook_function)post_call,cd);
    Carpet::Carpet_RegisterScheduleWrapper(pre_call,post_call);
    std::cout << "Hooks added" << std::endl;
    return 0;
  }
}

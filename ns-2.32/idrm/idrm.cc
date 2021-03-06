/*
  IDRM Code -- Developed by 
  SeungHoon Lee(UCLA CS Network Research Lab, shlee@cs.ucla.edu)
*/


// Standard Routing protcol headers
#include <aodv/aodv.h>
#include <dsdv/dsdv.h>			
#include <iostream>
#include <idrm/idrm.h>
#include <packet.h>
#include <ip.h>
#include <random.h>
#include <cmu-trace.h>
#include <common/mobilenode.h>

using namespace std;

#define CURR_TIME					Scheduler::instance().clock()
#define ALGORITHM_STATIC   0
#define ALGORITHM_DYNAMIC  1
#define ALGORITHM_ADAPTIVE 2

/****************************************************************
 CONFIG START
****************************************************************/

#define IDRM_ROUTE_EXPIRY			45								//11.5//45//10//6.0 // Anyy route entry updated by IDRM is valid for this interval
#define IDRM_NE_EXPIRY				3.0								// Time to expire route with hop_count = 1
#define IDRM_INTRA_GATEWAY_EXPIRY	20.0							//

///////////// Beacon /////////////////
#define BEACON_INTERVAL_MIN		5 //secs
#define BEACON_INTERVAL_MAX		20 //secs
#define BEACON_INTERVAL_INIT		20 // secs
#define BEACON_INTERVAL_RELAX_GRACE     5

///////////// Election /////////////////
#define ELECTION_PERIOD             100 //secs

///////////// Statistics /////////////////
#define STAT_DATA_START_TIME (ELECTION_PERIOD*2)

/****************************************************************
 CONFIG END
****************************************************************/

int NUM_DST;
int ADAPTATION_BEACON;

double* global_decision;
int global_decision_count=0;

//debugging setup
int IDRM_DEBUG			=	0;
int SH_DUMP			=	0;
int IDRM_SH_DEMO_IDRM     	=	0;
int IDRM_DEBUG_09		=	0;		//0902
int IDRM_DEBUG_0303		=	0;		//0902

int grid_test			=	0;
//0902
#define REQUEST_TIMEOUT		2000

int updateId			=	0;			//globally unique update Id
int rt_count			=	0;
int gateway_counter		=	0;

//////////////////////////////////////////////////////////////////////////////////////////////////
//For results	

FILE* idrm_output;									//record results!
int idrm_output_interval				=	10;		//result recording interval

long double total_pkts					=	0;
long double total_data_pkts				=	0;		//data packet across domains

long double total_eIdrm_pkts			=	0;		//overhead
long double total_iIdrm_pkts			=	0;		//overhead

long double total_drop_pkts				=	0;		//packet drop due to idrm route dynamics

long double total_new_routes			=	0;		//network dynamics
long double total_del_routes			=	0;		//network dynamics
long double total_upd_routes			=	0;


long double n_total_iidrm_pkt			=	0;
long double s_total_iidrm_pkt			=	0;

long double n_total_eidrm_pkt			=	0;
long double s_total_eidrm_pkt			=	0;

bool star = false;
bool Policy = false;


/*
  TCL Hooks
*/

int hdr_idrm::offset_;
static class IDRMHeaderClass : public PacketHeaderClass {
public:
  IDRMHeaderClass() : PacketHeaderClass("PacketHeader/IDRM",
					sizeof(hdr_all_idrm)) {
    bind_offset(&hdr_idrm::offset_);
  } 
} class_rtProtoIDRM_hdr;

static class IDRMclass : public TclClass {
public:
  IDRMclass() : TclClass("Agent/IDRM") {}
  TclObject* create(int argc, const char*const* argv) {
    assert(argc == 9);
    //return (new IDRM((nsaddr_t) atoi(argv[4])));
    return (new IDRM((nsaddr_t) Address::instance().str2addr(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7])));
  }
} class_rtProtoIDRM;



// ======================================================================
// ======================================================================
// EVENT/TIMER support 

void
idrmTimer::handle(Event *)
{
  busy_ = 0;
  agent->handlerTimer(type_);
}

void
idrmTimer::start(double time)
{
  assert(busy_ == 0);
  busy_ = 1;
  Scheduler::instance().schedule(this, &intr, time);
}

void
idrmTimer::cancel()
{
  assert(busy_ == 1);
  busy_ = 0;
  Scheduler::instance().cancel(&intr);
}

double
idrmTimer::timeLeft()
{
  assert(busy_ == 1);
  return intr.time_ - CURR_TIME;
}

// EVENT/TIMER support 

//------------------------------------------------//
/*--- cs218_idrm1 ---*/
// Initializing Gateways
//** need to replace this with inputs directly from tcl
void
IDRM::IniGateway()
{
	
  if (election_algorithm == ALGORITHM_STATIC) {
    double threshold = (double)num_intra_gateway * (double)pgw/ 100;
    if ((double)getIndex( myNodeId) < threshold) {
      now_gateway = true;
    }
    else {
      now_gateway = false;
    }
  }
  else
    now_gateway = true;
}

// Setting the Domain Policies
void
IDRM::IniMetricPolicy()
{
  // 0-> only connectivity
  // 1-> both connectivity and hop_count
  if (election_algorithm == ALGORITHM_ADAPTIVE || metricHopCount) {
    MetricPolicy = 1;
  }
  else {
    MetricPolicy = 0;
  }
}

// To change the color of the node when active
void 
IDRM::ToggleColor(void)
{
  Node *srcnode = Node::get_node_by_address(myNodeId);
  if(srcnode != NULL)
    {
      if(now_gateway)
	srcnode->now_gateway = true;
      else
	srcnode->now_gateway = false;
    }			
}
//------------------------------------------------//

int
IDRM::command(int argc, const char*const* argv) {
  printf("argc: %d\n", argc);
  if(argc == 9) {
    //0729
    int schannel = atoi(argv[7]);
		
    if( schannel ){ //single channel scenario
      if (strcmp(baseRAgentName, "AODV") == 0){
	((AODV*)baseRAgent)->target(target_);
      }
      else if (strcmp(baseRAgentName, "DSDV") == 0){
	((DSDV_Agent *)baseRAgent)->target(target_);
      }  
    } 
    //
		
    if(strncasecmp(argv[1], "start", 2) == 0) {
			
      strcpy( fname, argv[2] );
      beaconTimerSec					= atoi(argv[3]);		//e-IDRM Beacon Interval
      intraBeaconTimerSec				= atoi(argv[4]);		//i-IDRM Beacon Interval
      pollRTTimerSec					= 2.5;					//intraBeaconTimerSec; //sync IDRM control interval
      intraBeaconJitterSec			= 0.05;
      intra_gateway_expire			= BEACON_INTERVAL_MAX+ 20;	//cs218_idrm2 1222 
      	
			
      //cs218_idrm2 not used
      collectingTimerSec				= beaconTimerSec * 8.5;
      usingTimerSec					= beaconTimerSec * 20.5; //30.5;
			
      election_algorithm				= atoi(argv[5]);		//cs218_idrm2: should be percentage! 0 means static!
      printf("Electoin Algorithm: %d\n", election_algorithm);
			
      pgw								= atoi(argv[6]);		 //percentage gateway
      metricHopCount = 0;	
      collecting						= false;
			
      NUM_DST							= atoi(argv[8]);
      printf("NUM_DST is %d\n", NUM_DST);
      //TRACE_IDRM
      collectStatTimerSec = 100;
      activeGWStatTimerSec = 2;
      collectStatTimer.start(120);
      activeGWStatTimer.start(100);
      //END TRACE_IDRM	
			
      // cs218_idrm1 
      // The Node color Implementation
      strcpy(color,"red");
      Node *srcnode = Node::get_node_by_address(myNodeId);
      if(srcnode != NULL)
	{
	  strcpy(srcnode->color_idrm,color);
	  srcnode->now_gateway = false;
	}
			
      if (election_algorithm == ALGORITHM_ADAPTIVE) {
	ADAPTATION_BEACON = pgw;
      }
      //now_gateway						= true; //cs218_idrm2
			
      inter_neighbors_counter =0;
			
      if (baseRAgent != NULL){
				
	if (strcmp(baseRAgentName, "AODV") == 0){
					
	  if(IDRM_DEBUG)
	    printf("IDRM_DEBUG... Starting IDRM Agent on Node ID %d num_if %d my_if_id %d base_if %d base Routing Agent Name %s and index in it %d... \n",
		   myNodeId, numIfaces, myIface, baseIface, baseRAgentName, ((AODV *)baseRAgent)->index);
					
	  ((AODV *)baseRAgent)->idrmRAgent = (IDRM *) this; //IDRM_SH: moved!
					
	  pollRTTimer.start(IDRM_STARTUP_JITTER+pollRTTimerSec);
	  beaconTimer.start(IDRM_STARTUP_JITTER+beaconTimerSec);
	  intraBeaconTimer.start(IDRM_STARTUP_JITTER+intraBeaconTimerSec);
	  maintainRTTimer.start(IDRM_STARTUP_JITTER+maintainRTTimerSec);
	  recordResultTimer.start(0);
					
	  //num_intra_gateway = ((AODV *)baseRAgent)->num_gateway;
	  //cs218_idrm2 clean up
	  if( election_algorithm == ALGORITHM_STATIC){
	    num_intra_gateway	= ((AODV *)baseRAgent)->num_gateway;
	  }
	  else { //all node can be gateway
	    num_intra_gateway	= ((AODV *)baseRAgent)->num_domain_members;
	  }
					
	  if(IDRM_DEBUG_09){
	    printf("Verification NodeId: %d election_algorithm: %d num_intra_gt: %d num_dynamic_gt: %d ",
		   myNodeId, election_algorithm, num_intra_gateway, num_dynamic_gateways);	
	  }
					
	  intra_gateways = new nsaddr_t[num_intra_gateway];
					
	  for(int i=0; i<num_intra_gateway; i++){
	    intra_gateways[i] = ((AODV *)baseRAgent)->gateways[i];
	  }
					
	  intra_gateways_ts = new double[num_intra_gateway];
					
	  num_domain_members	= ((AODV *)baseRAgent)->num_domain_members;
					
	  memset(intra_gateways, -1, sizeof(nsaddr_t) * num_intra_gateway); // 0405
	  //0405
	  for(int i=0; i<num_domain_members; i++){ //half of nodes as potential gateways
	    intra_gateways[i] = getReverseIndex(i);
	  }
					
	  if( getIndex(myNodeId) >= num_domain_members ){
	    identity = false; //always non-gateway node 
	    now_gateway = false;
	  }
	  else
	    identity = true; //potential gateway
	  //0405
					
					
	  printf("0318 NodeId: %d myIntraGTs:", myNodeId);
	  for(int i=0; i<num_domain_members; i++){
	    printf("%d|", intra_gateways[i]);	
	  }
					
	  if(IDRM_DEBUG)
	    printf("\n");
					
	  //0412 tmp
	  //cs218_idrm1
	  IniGateway();
	  identity = true;
	  IniMetricPolicy();
					
	  for(int a=0; a< num_intra_gateway; a++){
	    intra_gateways_ts[a] = 0;
	  }
	  		
	  //0902
	  intra_neighbors				= new nsaddr_t[num_intra_gateway];
	  num_intra_neighbors			= 0;
					
	  //0307
	  intra_neighbors_ts			= new double[num_intra_gateway];
	  intra_neighbors_update		= new bool[num_intra_gateway];
	  memset(intra_neighbors_update, 1, sizeof(bool) * num_intra_gateway);
					
					
	  for(int i=0; i< num_intra_gateway; i++){
	    intra_neighbors_ts[i] = -1; 
	  }
					
	  printf("0307Verification NodeId: %d election_algorithm: %d num_intra_gt: %d myIntraGTs: ",
		 myNodeId, election_algorithm, num_intra_gateway);	
	  for(int i=0; i<num_intra_gateway; i++){
	    printf("| %d | ", intra_gateways[i]);
	  }
	  printf("\n");
					
	  //printf("0312 NodeId: %d, now_gateway: %d percentage: %lf election_algorithm: %d \n", myNodeId, now_gateway, pgw, election_algorithm);
					
	  //0316
	  inter_neighbors = new bool[num_intra_gateway];
	  memset(inter_neighbors, 0, sizeof(bool) * num_intra_gateway);
					
	  intra_neighbors_routes = new int[num_intra_gateway];	
	  memset(intra_neighbors_routes, 0, sizeof(int) * num_intra_gateway);
					
	  intra_neighbors_hops = new float[num_intra_gateway];
	  memset(intra_neighbors_hops, 0, sizeof(float) * num_intra_gateway);
					
	  sorted_gateways = new int[num_intra_gateway];
	  memset(sorted_gateways, -1, sizeof(int) * num_intra_gateway);
					
	  sorted_gateways_routes = new int[num_intra_gateway];
	  memset(sorted_gateways_routes, -1, sizeof(int) * num_intra_gateway);
					
	  sorted_gateways_after_selection = new int[num_intra_gateway];
	  memset(sorted_gateways_after_selection, -1, sizeof(int) * num_intra_gateway);
					
	  intra_neighbors_after_selection = new bool[num_intra_gateway];
	  memset(intra_neighbors_after_selection, -1, sizeof(bool) * num_intra_gateway);
					
	  //cs218_idrm2 : adaptation
	  inter_nodes			= new int[NUM_DST];
	  memset(inter_nodes,		0,	sizeof(int) * NUM_DST);
					
	  intra_nodes			= new int[num_intra_gateway];
	  memset(inter_nodes,		0,	sizeof(int) * num_intra_gateway);
					
	  inter_nodes_old		= new int[NUM_DST];
	  memset(inter_nodes_old, 0,	sizeof(int) * NUM_DST);
					
	  intra_nodes_old		= new int[num_intra_gateway];
	  memset(inter_nodes_old, 0,	sizeof(int) * num_intra_gateway);	
					
	  my_reachables		= new bool[NUM_DST];
	  memset(my_reachables,	0,	sizeof(bool) * NUM_DST);
					
	  if (myNodeId == 0) {
	    global_decision	= new double[NUM_DST*NUM_DST];
	    memset(global_decision, 0, sizeof(double) * NUM_DST * NUM_DST);
						
	  }
	  others_reachables = new int[NUM_DST*num_intra_gateway];
	  memset(others_reachables, 0, sizeof(int) * num_intra_gateway * NUM_DST);
					
	  my_dst = new bool[NUM_DST];
	  memset(my_dst,	0, sizeof(bool) * NUM_DST);
	  dst_from_eIDRM = new bool[NUM_DST];
	  memset(dst_from_eIDRM,	0, sizeof(bool) * NUM_DST);	
	  my_hops = new int[NUM_DST];
	  memset(my_hops,	0, sizeof(int) * NUM_DST);	
	  hops_from_eIDRM = new int[NUM_DST];
	  memset(hops_from_eIDRM,	0, sizeof(int) * NUM_DST);
	}
	else if (strcmp(baseRAgentName, "DSDV") == 0){
					
	  if(IDRM_DEBUG)				
	    printf("IDRM_DEBUG... Starting IDRM Agent on Node ID %d num_if %d my_if_id %d base_if %d base Routing Agent Name %s and index in it %d... \n",
		   myNodeId, numIfaces, myIface, baseIface, baseRAgentName, ((DSDV_Agent *)baseRAgent)->myaddr_);
					
	  ((DSDV_Agent *)baseRAgent)->idrmRAgent = (IDRM *) this;
					
	  pollRTTimer.start(IDRM_STARTUP_JITTER+pollRTTimerSec);
	  beaconTimer.start(IDRM_STARTUP_JITTER+beaconTimerSec);
	  intraBeaconTimer.start(IDRM_STARTUP_JITTER+intraBeaconTimerSec + 50);
	  maintainRTTimer.start(IDRM_STARTUP_JITTER+maintainRTTimerSec);
	  recordResultTimer.start(0); //0623
					
					
					
	  //cs218_idrm2 clean up
	  if( election_algorithm == ALGORITHM_STATIC ){
	    num_intra_gateway		= ((DSDV_Agent *)baseRAgent)->num_gateway;
	  }
	  else { //all node can be gateway
	    num_intra_gateway		= ((DSDV_Agent *)baseRAgent)->num_domain_members;
	  }
					
	  intra_gateways = new nsaddr_t[num_intra_gateway];
					
	  for(int i=0; i<num_intra_gateway; i++){
	    intra_gateways[i] = ((DSDV_Agent *)baseRAgent)->gateways[i];
	  }
					
	  intra_gateways_ts = new double[num_intra_gateway];
	  intra_neighbors_update		= new bool[num_intra_gateway];
	  memset(intra_neighbors_update, 1, sizeof(bool) * num_intra_gateway);
					
	  //memset(intra_gateways_ts, 0, sizeof(double) * num_intra_gateway); //initially GTs are all in the domain
	  for(int a=0; a< num_intra_gateway; a++){
	    intra_gateways_ts[a] = 0;
	  }
					
	  num_domain_members	= ((DSDV_Agent *)baseRAgent)->num_domain_members;
					
	  if(IDRM_DEBUG){
	    printf("Verification NodeId: %d election_algorithm: %d num_intra_gt: %d, domain_members: %d ",
		   myNodeId, election_algorithm, num_intra_gateway, num_domain_members);	
	  }
					
					
	  //
	  IniGateway();
					
	  printf("ELECTION_ALGORITHM = %d",election_algorithm);
	  for(int i=0; i<num_domain_members; i++){
	    intra_gateways[i] = getReverseIndex(i); //0318
	  }
					
	  int index = getIndex(myNodeId);
					
	  //if(index >= num_domain_members/2){
	  if(index >= num_domain_members) {
	    identity = false;
	    now_gateway = false;
	    ((DSDV_Agent *)baseRAgent)->my_gateway_list[index] = 0; 
	  }
	  else {
	    identity = true;
	    printf("NodeId: %d index: %d\n", myNodeId, index);
	    ((DSDV_Agent *)baseRAgent)->my_gateway_list[index] = 1; 
	    IniMetricPolicy();
	  }
					
	  //0404
					
	  printf("0318 NodeId: %d myIntraGTs:", myNodeId);
	  for(int i=0; i<num_domain_members; i++){
	    printf("%d|", intra_gateways[i]);	
	  }
					
	  if(IDRM_DEBUG)
	    printf("\n");
					
					
	  //0902
	  intra_neighbors				= new nsaddr_t[num_intra_gateway];
	  num_intra_neighbors			= 0;
					
	  //0307
	  intra_neighbors_ts			= new double[num_intra_gateway];
					
	  for(int i=0; i< num_intra_gateway; i++){
	    intra_neighbors_ts[i] = -1; 
	  }
					
	  printf("0307Verification NodeId: %d election_algorithm: %d num_intra_gt: %d myIntraGTs: ",
		 myNodeId, election_algorithm, num_intra_gateway);	
	  for(int i=0; i<num_intra_gateway; i++){
	    printf("| %d | ", intra_gateways[i]);
	  }
	  printf("\n");
					
	  printf("0312 NodeId: %d, now_gateway: %d percentage: %lf election_algorithm: %d \n", myNodeId, now_gateway, pgw, election_algorithm);
					
	  //0316
	  inter_neighbors = new bool[num_intra_gateway];
	  memset(inter_neighbors, 0, sizeof(bool) * num_intra_gateway);
					
	  intra_neighbors_routes = new int[num_intra_gateway];
	  memset(intra_neighbors_routes, 0, sizeof(int) * num_intra_gateway);
					
	  intra_neighbors_hops = new float[num_intra_gateway];
	  memset(intra_neighbors_hops, 0, sizeof(float) * num_intra_gateway);
					
	  sorted_gateways = new int[num_intra_gateway];
	  memset(sorted_gateways, -1, sizeof(int) * num_intra_gateway);
					
	  sorted_gateways_routes = new int[num_intra_gateway];
	  memset(sorted_gateways_routes, -1, sizeof(int) * num_intra_gateway);
					
	  sorted_gateways_after_selection = new int[num_intra_gateway];
	  memset(sorted_gateways_after_selection, -1, sizeof(int) * num_intra_gateway);
					
	  intra_neighbors_after_selection = new bool[num_intra_gateway];
	  memset(intra_neighbors_after_selection, -1, sizeof(bool) * num_intra_gateway);
					
	  //cs218_idrm2 : adaptation init
	  inter_nodes			= new int[NUM_DST];
	  memset(inter_nodes,		0, sizeof(int) * NUM_DST);
					
	  intra_nodes			= new int[num_intra_gateway];
	  memset(inter_nodes,		0, sizeof(int) * num_intra_gateway);
					
	  inter_nodes_old		= new int[NUM_DST];
	  memset(inter_nodes_old, 0, sizeof(int) * NUM_DST);
					
	  intra_nodes_old		= new int[num_intra_gateway];
	  memset(inter_nodes_old, 0, sizeof(int) * num_intra_gateway);
					
	  my_reachables	= new bool[NUM_DST];
	  memset(my_reachables, 0, sizeof(bool) * NUM_DST);
					
	  if(myNodeId == 0) {
	    global_decision	= new double[NUM_DST*NUM_DST];
	    memset(global_decision, 0, sizeof(double) * NUM_DST * NUM_DST);

	  }
	  
		others_reachables = new int[NUM_DST*num_intra_gateway];
	  memset(others_reachables, 0, sizeof(int) * num_intra_gateway * NUM_DST);

	  my_dst = new bool[NUM_DST];
	  memset(my_dst,	0, sizeof(bool) * NUM_DST);
	  dst_from_eIDRM = new bool[NUM_DST];
	  memset(dst_from_eIDRM,	0, sizeof(bool) * NUM_DST);	
	  my_hops = new int[NUM_DST];
	  memset(my_hops,	0, sizeof(int) * NUM_DST);	
	  hops_from_eIDRM = new int[NUM_DST];
	  memset(hops_from_eIDRM,	0, sizeof(int) * NUM_DST);
					
	  printf("Verification2 NodeId: %d num_intra_gateway: %d num_domain_members: %d, NUM_DST: %d ",
		 myNodeId, num_intra_gateway, num_domain_members, NUM_DST);	
					
	}
	else if (strcmp(baseRAgentName, "TORA") == 0){//will be implemented..
	}
	else if (strcmp(baseRAgentName, "DSR") == 0){//will be implemented..
	}
	return TCL_OK;
      }
    }
  } else if (argc == 4) {
    if (strcmp(argv[1], "set-baseagent") == 0) {
      baseRAgent = (Agent*) TclObject::lookup(argv[2]);
      strcpy(baseRAgentName, argv[3]);
      assert(baseRAgent);
      return TCL_OK;
    }
    if (strcmp(argv[1], "set-if-id") == 0) {
      myIface = atoi(argv[2]);
      myIfaceId = atoi(argv[3]);
      return TCL_OK;
    }
    if (strcmp(argv[1], "if-queue") == 0) {
      PriQueue * ifq = (PriQueue *) TclObject::lookup(argv[3]);
      iqueue = ifq; //0806
      int temp_ = atoi(argv[2]);
      if (temp_ >= numIfaces){
	temp_ = numIfaces;
	numIfaces++;
      }
      ifqueuelist[temp_] = ifq;
      if (ifqueuelist[temp_]){
	return TCL_OK;
      } else {
	return TCL_ERROR;
      }
    }
    if (strcmp(argv[1], "target") == 0) {
      int temp_ = atoi(argv[2]);
      if (temp_ >= numIfaces){
	temp_ = numIfaces;
	numIfaces++;
      }
      targetlist[temp_] = (NsObject *) TclObject::lookup(argv[3]);
      target_ = targetlist[myIface];
      if (targetlist[temp_]){
	return TCL_OK;
      } else {
	return TCL_ERROR;
      }
    }
  }
  return Agent::command(argc, argv);
}

/* 
   Constructor
*/
IDRM::IDRM(nsaddr_t id, int numifs, int my_if_id, int base_if_id) : 
  Agent(PT_IDRM),
  pollRTTimer(this, POLL_PULL_RT_TIMER),
  beaconTimer(this, PERIODIC_BEACON_TIMER),
  maintainRTTimer(this, MAINTAIN_RT_TIMER),
  broadcastRTTimer(this, BROADCAST_RT_TIMER),
  intraBeaconTimer(this, PERIODIC_INTRA_BEACON_TIMER),						//i-IDRM Beacon
  detectPartitionTimer(this, PERIODIC_PARTITION_DETECTION_TIMER), //Detecting domain partition
  recordResultTimer(this, RECORD_RESULT_TIMER),										//Recording result timer
  //0902
  collectingTimer(this, COLLECTING_TIMER),
  usingTimer(this, USING_TIMER),
  intraJitterTimer(this,INTRA_BEACON_JITTER_TIMER),
  //TRACE_IDRM
  collectStatTimer(this, COLLECT_RESULTS_TIMER),
  activeGWStatTimer(this, ACTIVEGATEWAY_STATS_TIMER) 
  //END TRACE_IDRM	
{
  numIfaces  = numifs;
  myIface	   = my_if_id;
  myNodeId   = id;
  baseIface  = base_if_id;
  baseRAgent = NULL;
  strcpy(baseRAgentName, "NULL");
  pollRTTimerSec				= 15.0; //routing table exchange between IDRM and baseRouting Agent
  maintainRTTimerSec			= 5; //0.1;
  broadcastRTTimerSec			= 1.0;
  beaconId					= 0;
  n_idrm_rt_entry				= 0;	// no. of entry in the IDRM route table
  n_intra_idrm_rt_entry		= 0;
	
  //e-IDRM, i-IDRM Table Init...
  memset( idrm_rt_table, -1, sizeof(struct idrm_rt_entry) * MAX_ROUTE);
  memset( intra_idrm_rt_table, -1, sizeof(struct idrm_rt_entry) * MAX_ROUTE);
	
  detectPartitionTimerSec = 50.0;
  currentIntraBeacon		= 0;			
  recordResultTimerSec	= 10.0;
	
  n_out_beacon	=0;
  n_out_update	=0;
  n_out_table		=0;
  n_out_request	=0; 
  n_out_data		=0;
	
  s_out_beacon	=0;
  s_out_update	=0;
  s_out_table		=0;
  s_out_request	=0; 
  s_out_data		=0;
	
  n_total_out_eidrm	=0;
  n_total_out_iidrm	=0;
  n_total_out_data	=0;
	
  s_total_out_eidrm	=0;
  s_total_out_iidrm	=0;
  s_total_out_data	=0;
	
  n_out_intra_beacon	=0;
  n_out_intra_update	=0;
  n_out_intra_table	=0;
  n_out_intra_request =0; 
  n_out_intra_data	=0;
	
  s_out_intra_beacon	=0;
  s_out_intra_update	=0;
  s_out_intra_table	=0;
  s_out_intra_request =0; 
  s_out_intra_data	=0;
	
  n_in_beacon		=0;
  n_in_update		=0;
  n_in_table		=0;
  n_in_request	=0; 
  n_in_data		=0;
	
  s_in_beacon		=0;
  s_in_update		=0;
  s_in_table		=0;
  s_in_request	=0; 
  s_in_data		=0;
	
  n_in_intra_beacon	=0;
  n_in_intra_update	=0;
  n_in_intra_table	=0;
  n_in_intra_request	=0; 
  n_in_intra_data		=0;
	
  s_in_intra_beacon	=0;
  s_in_intra_update	=0;
  s_in_intra_table	=0;
  s_in_intra_request	=0; 
  s_in_intra_data		=0;
	
  n_total_in_eidrm	=0;
  n_total_in_iidrm	=0;
  n_total_in_data		=0;
	
  s_total_in_eidrm	=0;
  s_total_in_iidrm	=0;
  s_total_in_data		=0;
	
  idrm_rt_seq			= 1000;
  intra_idrm_rt_seq	= 1000;
	
  //memset(intra_updated_entry, -1, sizeof(nsaddr_t) * MAX_ROUTE);
  for(int a=0; a<MAX_ROUTE; a++){
    intra_updated_entry[a] = -1;	
  }
	
  my_idrm_payload	 = new char[ sizeof(idrm_rt_entry) ];
	
  trackRequest = false;
  exchange = 0;
  now_gateway = false;
	
  beacon_counter =0;
	
  memset(my_dst, 0, sizeof(bool)*NUM_DST);
  memset(dst_from_eIDRM, 0, sizeof(bool)*NUM_DST);
	
  new_gateway = false;
	
  //cs218_idrm2 vincent: adaptation
  intra_beacon_grace		= 0;
  beacon_grace			= 0;
  intra_last_update_ts	= 0.0;
  inter_last_update_ts	= 0.0;
  lastElectionTs			= 0;
	
  decision_check = false;
  intra_count_decision =0;
}

// Top and other EVENT handler functions
void
IDRM::handlerTimer(idrmTimerType t)
{
  switch(t) {
  case POLL_PULL_RT_TIMER:
    pollRTTimerHandler();
    break;
  case PERIODIC_BEACON_TIMER:
    beaconTimerHandler();
    break;
  case MAINTAIN_RT_TIMER:
    maintainRTHandler();
    break;
  case BROADCAST_RT_TIMER:
    broadcastRTHandler();
    break;
  case PERIODIC_INTRA_BEACON_TIMER:
    intraBeaconTimerHandler(); //need to check!
    break;
  case PERIODIC_PARTITION_DETECTION_TIMER:
    detectPartitionTimerHandler();
    break;
  case RECORD_RESULT_TIMER:
    recordResultTimerHandler();
    break;	
    //0902
  case COLLECTING_TIMER:
    collectingTimerHandler();
    break;
  case USING_TIMER:
    usingTimerHandler();
    break;
  case INTRA_BEACON_JITTER_TIMER:
    intraBeaconJitterTimerHandler();
    break;
    //0902
    //TRACE_IDRM
  case COLLECT_RESULTS_TIMER:
    collectStatTimerHandler();
    break;
  case ACTIVEGATEWAY_STATS_TIMER:
    activeGWStatTimerHandler();
    break;
    //END TRACE_IDRM
  default:
    abort();
  }
}

//0902
void
IDRM::collectingTimerHandler(void){
}

//0902
void
IDRM::usingTimerHandler(void){
}
//0902

//TRACE_IDRM
void
IDRM::collectStatTimerHandler(void){
	
  // CAN PRINT ALL THE STATS HERE
  collectStatTimer.start(collectStatTimerSec);
	
}
void
IDRM::activeGWStatDump(void) {
	
  if( strcmp(baseRAgentName,"DSDV") != 0 ) {
    printf("IDRM_ERROR: Connectivity dump is only supported for DSDV");
    exit(1);
  }
	
  int intra_connectivity = 0;
  int inter_connectivity = 0;
  int intrainter_connectivity =0;
  rtable_ent *prte;
  RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
	
  if(election_algorithm == ALGORITHM_STATIC && pgw == 0) {
    for (int i = 0; i< NUM_DST; i++) {
      if( (prte = rt->GetEntry(i)) !=NULL && prte->metric < 250 ) {
	intra_connectivity++;
      }
    }
  }
  else {
    if(now_gateway) {

      for (int i = 0; i< NUM_DST; i++) {
	if( (prte = rt->GetEntry(i)) !=NULL && prte->metric < 250 ) {
	  intra_connectivity++;
	}
      }

      //inter_connectivity = rt_idrm_lookup();
      int totalconn = n_idrm_rt_entry;
      for (int i = 0; i< NUM_DST; i++) {
	if( !rt_lookup(i) && (prte = rt->GetEntry(i)) && prte->metric < 250 )
	  totalconn++;
      }
      //intra_connectivity = totalconn - inter_connectivity;
      inter_connectivity = n_idrm_rt_entry - intra_connectivity;
      if(inter_connectivity < 0 ) inter_connectivity =0;

      int tmp = rt_idrm_lookup();
      intrainter_connectivity =  totalconn - tmp;
    }
		
  }
	
  int domain = (int)(myNodeId / num_domain_members);
	
  printf("\n");
  if( CURR_TIME > STAT_DATA_START_TIME ) {
    printf("STAT NodeId: %d Active: %d Domain: %d Conn: %d interconn: %d @Time: %lf IntraInter: %d\n",
	   myNodeId,now_gateway,domain,(inter_connectivity + intra_connectivity),inter_connectivity,CURR_TIME,intrainter_connectivity);
  }
}


void
IDRM::activeGWStatTimerHandler(void){
	
  //int offset = getIndex(myNodeId)*NUM_DST;
	
  // Computing the Inter and Intra Connectivities from my_reachables
  /*for (int i = 0; i< NUM_DST; i++) {
    others_reachables[offset+i] = (int)my_reachables[i];
    }
  */
  //activeGWStatTimer.start(activeGWStatTimerSec);
}
//END TRACE_IDRM



void
IDRM::intraBeaconJitterTimerHandler(void){
	
  if(myNodeId != intra_gateways[gateway_index])
    sendIntraBeacon(intra_gateways[gateway_index++]);
  else
    gateway_index++;
	
  if(IDRM_DEBUG)
    printf("Sent intra Becon to %d from %d at %lf\n",intra_gateways[gateway_index],myNodeId,CURR_TIME);
	
	
	
  if(gateway_index<num_intra_gateway)
    {
      intraJitterTimer.start(intraBeaconJitterSec);
    }
}


void
IDRM::pollRTTimerHandler(void)
{
  //printf("IDRM_DEBUG POLL Routing Table Read from Base Agent on Node %d at %lf\n",myNodeId, CURR_TIME);
  maintainIntraRT();
	
  //if(now_gateway == false) //0303
  if (strcmp(baseRAgentName, "AODV") == 0){
    aodvRTReadTrackChanges();
  } else if (strcmp(baseRAgentName, "DSDV") == 0){
    dsdvRTReadTrackChanges();				
  } else if (strcmp(baseRAgentName, "DSR") == 0){
    dsrRTReadTrackChanges();				
  } else if (strcmp(baseRAgentName, "TORA") == 0){
  } else {
    printf("IDRM_ERROR: Unknown Base routing agent on Node %d at %lf\n",myNodeId, CURR_TIME);
    exit(1);
  }
	
  // Now schedule the next event
  pollRTTimer.start(pollRTTimerSec);
}

void
IDRM::beaconTimerHandler(void)
{
  //printf("IDRM_DEBUG BEACON Timer on Node %d at %lf\n",myNodeId, CURR_TIME);
	
  sendBeacon();
	
  //maintainIntraRT();
	
  //print out baseAgent overhead
  if (strcmp(baseRAgentName, "AODV") == 0){
    //	((AODV *)baseRAgent)->printOverhead();
  }else if (strcmp(baseRAgentName, "DSDV") == 0){
    ((DSDV_Agent *)baseRAgent)->printOverhead();
  } else if (strcmp(baseRAgentName, "DSR") == 0){ //will be implemented..
  } else if (strcmp(baseRAgentName, "TORA") == 0){
  } else {
    printf("IDRM_ERROR: Unknown Base routing agent on Node %d at %lf\n",myNodeId, CURR_TIME);
    exit(1);
  }
	
  if( ADAPTATION_BEACON && election_algorithm == ALGORITHM_ADAPTIVE )
    adaptationRecalculateBeaconInterval(); //vincent: adaptation CS218 IDRM2
	
  // Now schedule the next event
  beaconTimer.start(beaconTimerSec);
}

void
IDRM::maintainRTHandler(void) {
  maintainRT();
  maintainRTTimer.start(maintainRTTimerSec);
}


void
IDRM::broadcastRTHandler(void)
{
  //sendRouteTable(IP_BROADCAST, 0);
	
  //for demo
  if(myNodeId == 0)
    printPositions();
	
  broadcastRTTimer.start(broadcastRTTimerSec);
}

void
IDRM::printPositions(){
	
  if(IDRM_SH_DEMO_IDRM){
    for(int i=0; i<num_domain_members*2; i++){
			
			
      if( i== 2 || i== 3 || i== 12 || i== 14 || i== 15) continue; //for case2
      MobileNode* mynode = (MobileNode*)Node::get_node_by_address(i);
			
      //<node time="1" id="0" x="100" y="600" /> 
      //printf("position: %d %.2f %.2f %.2f\n", addr, mynode->X(), mynode->Y());
      printf("<node time=\"%.0lf\" id=\"%d\" x=\"%.0f\" y=\"%.0f\" />\n",
	     CURR_TIME, i, mynode->X(), mynode->Y());
    }
		
    printf("<node time=\"%.0lf\" id=\"20\" x=\"800\" y=\"700\" />\n", CURR_TIME);
    printf("<node time=\"%.0lf\" id=\"21\" x=\"600\" y=\"800\" />\n", CURR_TIME);
    printf("<node time=\"%.0lf\" id=\"22\" x=\"450\" y=\"750\" />\n", CURR_TIME);
    printf("<node time=\"%.0lf\" id=\"23\" x=\"600\" y=\"400\" />\n", CURR_TIME);
    printf("<node time=\"%.0lf\" id=\"24\" x=\"300\" y=\"320\" />\n", CURR_TIME);
  }
}

//------------------------------------------------//
/*--- IDRM1 ---*/

void
IDRM::intraBeaconTimerHandler(void)
{
  if(IDRM_DEBUG)
    cout<<"intraBeaconTimerHandler :"<<myNodeId<<" Active "<<now_gateway<<endl;
  if(IDRM_DEBUG){
    printf("11712 NodeId: %d ,sh_d %d, num:%d at %lf\t myIntraGWs: ", myNodeId, election_algorithm,num_intra_gateway, CURR_TIME);
    for(int i=0; i< num_intra_gateway; i++){
      printf("%d ", intra_gateways[i]);
    }
    printf("\n");	
    dumpRT();
  }
	
  int index = getIndex(myNodeId);
  // This is the first round initialize self's connectivity and hop count
  if(beacon_counter == 1) {
    intra_neighbors_routes[index] = 0;
    intra_neighbors_hops[index] = 0;
  }
	
  intra_neighbors_ts[index] = CURR_TIME + intra_gateway_expire;
  //TRACE_IDRM
  //nodeID + DomainID + number of unique connections (like above, but maybe for each GW) + array of the destinations
	
	
  //END TRACE_IDRM
	
  //0307: detecting domain partition
  // Update the Connectivity ,Hop Count Data to be used in the Current Round
	
  //cs218_idrm2
	double decision_p=0;
	double c=1;
  //0214
  if(election_algorithm == ALGORITHM_ADAPTIVE){
	
    if(!decision_check){
      for(int i=0; i<num_intra_gateway; i++){
	
				if(my_reachables[myNodeId-index+i]    == 1){                                                                                                                           
				//if( intra_gateways_ts[i] > CURR_TIME ){                                                                                                                                                               
			  	c++;
				}
			}
     
    //decision_check = true;
    global_decision_count++;	

    if( c == 1){
			now_gateway = true;
			//decision_check = true;
     }
     else{
				//double decision_p =  global_decision[myNodeId] /  intra_count_decision * 100;
				decision_p =  global_decision[myNodeId] /  c * 100;
							
				//if(global_decision[myNodeId]){
				if( decision_p >= 20.0){
				//if(  global_decision[myNodeId] > 0){
					  now_gateway = true;
					  //decision_check = true;
				}
     }

		//printf("0214SH nodeID: %2d #intra: %f, global: %f decision_p: %f at %lf\n", myNodeId, c, global_decision[myNodeId], decision_p, Scheduler::instance().clock());
   }


    decision_check = true;
				
    if(global_decision_count == NUM_DST){
      //all checked, reset
     // printf("0214SH Reset NodeID: %d at %lf\n", myNodeId, CURR_TIME);
      memset(global_decision, 0, sizeof(double) * NUM_DST);
      global_decision_count =0;
			
			memset(my_reachables, 0, sizeof(bool)*NUM_DST);
			memset(others_reachables, 0, sizeof(int)*NUM_DST*num_intra_gateway);
    }
  }
	
  if (CURR_TIME - lastElectionTs > ELECTION_PERIOD) {
		
    lastElectionTs = CURR_TIME;
		
    if(election_algorithm == ALGORITHM_STATIC) {
			
      activeGWStatDump();
      memset(dst_from_eIDRM, 0, sizeof(bool)*NUM_DST);
      memset(inter_neighbors, 0, sizeof(bool) * num_intra_gateway);		
      memset(intra_neighbors_routes, 0, sizeof(int) * num_intra_gateway);
      memset(intra_neighbors_hops, -1, sizeof(float) * num_intra_gateway);
      memset(sorted_gateways, -1, sizeof(int) * num_intra_gateway);
			
      memset(my_reachables, 0, sizeof(bool)*NUM_DST);
      //memset(others_reachables, 0, sizeof(int)*NUM_DST*num_intra_gateway);
      //memset(excluded_reachables, 0, sizeof(bool)*NUM_DST);
      //0323
      memset(intra_neighbors_update, 1, sizeof(bool) * num_intra_gateway);
    }
    else
      {

	if( election_algorithm == ALGORITHM_ADAPTIVE && ADAPTATION_BEACON )
	  adaptationRecalculateIntraBeaconInterval(); //cs218_idrm2
			
	activeGWStatDump();
			
	// --- Partition part - Currently not in use 
	bool partition = false;
	//int index = getIndex(myNodeId);

	memset(intra_neighbors_after_selection, 0, sizeof(bool)* num_intra_gateway);
	for(int i=0; i< num_intra_gateway; i++){
	  intra_neighbors_after_selection[i] = true;
	  if( intra_neighbors_ts[i] > 0 && intra_neighbors_ts[i] < CURR_TIME ){
	    printf("Inside partition area!");
	    intra_neighbors_ts[i] = -1;
	    partition = true;	
	  }
	}
			
	after_selection_my_routes = intra_neighbors_routes[index];	//0324
			
	/* cs218_idrm2: election */
			
	if( election_algorithm == ALGORITHM_ADAPTIVE ){
	  adaptationDynamicGatewayElection();
	  decision_check = false; //0214
	  intra_count_decision=0;
	}
	else {
	  printf("vincent in setgateway");
	  setGateway(); //0311
	  new_gateway = false;
	}
	/* end of election */
			
	// --- 
			
	// 
	// Get the number of routes in the Base Routing Table(AODV,DSDV)
	int base_routes = base_lookup();
			
	//for iIDRM beacon
			
	// Compute the AvgHop and the Connectivity metrics to be used in the current round of elections
	int my_routes_eIDRM_in =0;
	float AvgHop = 0;
	int val = 0;
	printf("My Dests %d\n",myNodeId);
	
	for(int i=0; i<NUM_DST; i++){
	  printf("%d-%d-%d-(%d,%d), ",i,hops_from_eIDRM[i], my_reachables[i], (int)(i / num_domain_members), (int)(myNodeId / num_domain_members));
	  
	  if(my_reachables[i] && (int)(i / num_domain_members) != (int)(myNodeId / num_domain_members)) {
	    printf("%d-%d, ",i,hops_from_eIDRM[i]);
	    my_routes_eIDRM_in++;
	    if( hops_from_eIDRM[i] > 0 && hops_from_eIDRM[i] < 100){
	      AvgHop = AvgHop + hops_from_eIDRM[i];
	      val++;
	    }
	  }
	}
	if(val > 0)
	  AvgHop = AvgHop/val;
	else
	  AvgHop = 0;
       
	//for eIDRM beacon
			
	// Set the Connectivity and HopCount information to broadcast in the current interval
	memset(my_dst, 0, sizeof(bool)*NUM_DST);
	
	for(int i=0;i<NUM_DST;i++)
	  {
	    hops_from_eIDRM[i] = 100;
	    my_hops[i] = 100;
	  }
			
	//printf("\nSetting Dest : %d\n",myNodeId);
	for(int i=0;i<n_idrm_rt_entry;i++){
	  if(idrm_rt_table[i].dst >= 0 && idrm_rt_table[i].dst < NUM_DST && idrm_rt_table[i].route_type != 'D')
	    {my_dst[idrm_rt_table[i].dst] = 1; 
	      my_hops[idrm_rt_table[i].dst] = (idrm_rt_table[i].hop_count+1);
	      hops_from_eIDRM[idrm_rt_table[i].dst] = idrm_rt_table[i].hop_count;
					
	     // printf("(%d,%d) ",idrm_rt_table[i].dst,my_hops[idrm_rt_table[i].dst]);
	    }
	}
			
			
	//printf("myNodeId : %d - AvgHop : %f - val : %d \n",myNodeId,AvgHop,val);
			
			
	memset(dst_from_eIDRM, 0, sizeof(bool)*NUM_DST);
	memset(inter_neighbors, 0, sizeof(bool) * num_intra_gateway);
	memset(intra_neighbors_routes, 0, sizeof(int) * num_intra_gateway);
	memset(intra_neighbors_hops, -1, sizeof(float) * num_intra_gateway);
	memset(sorted_gateways, -1, sizeof(int) * num_intra_gateway);
				
	//memset(excluded_reachables, 0, sizeof(bool)*NUM_DST);
	//0323
	memset(intra_neighbors_update, 1, sizeof(bool) * num_intra_gateway);
			
	
	intra_neighbors_routes[index] = my_routes_eIDRM_in + base_routes;
	intra_neighbors_hops[index] = AvgHop;
	
	if(IDRM_DEBUG)		
		printf("0317 NodeId: %d my_routes_to_iIDRM: %d( %d + %d ) at %lf \n", 
	       myNodeId,intra_neighbors_routes[index], my_routes_eIDRM_in, base_routes, CURR_TIME);
	if(IDRM_DEBUG)				
		printf("0316 NodeId: %d now_gateway: %d at %lf\n", myNodeId, now_gateway, CURR_TIME);
      }
  }
	
  //0307
  //cs218_idrm2 moved this block down to send beacons after election is complete
  // Change the color of the node when active
  ToggleColor();				
	
  gateway_index = 0;
	
  // When sending intra beacons jitter the transmissions, sending the intra beacons with a 1 second jitter
  intraJitterTimer.start(intraBeaconJitterSec); // send all intra beacons (with jitter)
	
  // Tracking rounds
  beacon_counter++;
	
  intraBeaconTimer.start(intraBeaconTimerSec);
}

//------------------------------------------------//


void
IDRM::detectPartitionTimerHandler(void)
{
  if(!detect_partition()) //MANET is partitioned
    setMANET_ID();
	
  detectPartitionTimer.start(detectPartitionTimerSec);
}

void
IDRM::recordResultTimerHandler(void)
{
  idrm_result_record();
  recordResultTimer.start(recordResultTimerSec);
}

// ======================================================================
// eIDRM
void
IDRM::maintainRT() {
	
  if(IDRM_DEBUG)
    cout<<"maintain rt :"<<myNodeId<<endl;
  if( n_idrm_rt_entry == 0 ) return;
	
  // Frequently check for
  // 1) New neighbor, call sendRouteTableRequest()
  // 2) New/Delete/Expire/Update route, send out route update packet
  // copy idrm_route_table to old_idrm_route_table at the end
	
  int i,j,k;
  int change;
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG Node %d Before maintainRT at %.4lf\n", myNodeId, CURR_TIME);
  //dumpRT();
	
  // Delete Expire route
  // We will send the update of deleted route later in this function
  for( i=0;i<n_idrm_rt_entry;i++)	 {
    if( idrm_rt_table[i].expire_t < CURR_TIME) {
      idrm_rt_table[i].route_type = 'D';
      total_del_routes++;
      //my_reachables[idrm_rt_table[i].dst] = 0; //cs218 idrm2 timestamp check
    }
    if (!now_gateway) {
      if (idrm_rt_table[i].dst / num_domain_members != 
	  myNodeId / num_domain_members) //if dst is not in this domain
	idrm_rt_table[i].route_type = 'D';
    }
  }
	
  //recursive check deleted route
  change = 1;
  while( change ) {
    change = 0;
    for( i=0;i<n_idrm_rt_entry;i++)
      if( idrm_rt_table[i].route_type == 'D' ) {
	for(j=0;j<n_idrm_rt_entry;j++) 
	  if( idrm_rt_table[j].route_type != 'D' ) {
	    for(k=0;k<idrm_rt_table[j].hop_count;k++) {
	      if( idrm_rt_table[j].path[k] == idrm_rt_table[i].dst) {
		idrm_rt_table[j].route_type = 'D';				
		change	= 1;
	      }
	    }
	  }
      }
  }
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG Node %d After maintainRT at %.4lf\n", myNodeId, CURR_TIME);
  dumpRT();
	
  sendIntraRouteUpdate();
	
  //printf("NodeId:%d IntraRouteUpdate() called\n", myNodeId);
  sendRouteUpdate(EIDRM);
}

void
IDRM::sendIntraRouteUpdate(){
	
  if(IDRM_DEBUG)
    cout<<"sendintrarouteupdate :"<<myNodeId<<endl;
	
  if(identity == false) return; //0404
	
  for(int i=0; i < num_intra_gateway; i++){
		
    //send update msg to the intra-GTs currently valid
    if(election_algorithm == ALGORITHM_STATIC){ //static
      if( intra_gateways_ts[i] > CURR_TIME ){
	sendIntraRTUpdate( 0, intra_gateways[i] );			//jitter set inside sendIntraRTUdate()!
      }
    }
    else{// dynamic
      int index = getIndex(intra_gateways[i]);
			
      //if(intra_neighbors_update[index] == false)
      //	printf("0323 NodeId: %d not send intraRTU to %d at %lf\n", myNodeId, intra_gateways[i], CURR_TIME);
			
      if(intra_neighbors_update[index] && intra_gateways_ts[i] > CURR_TIME ){
	//printf("0323 NodeId: %d sent intraRTU to %d at %lf\n", myNodeId, intra_gateways[i], CURR_TIME);
	sendIntraRTUpdate( 0, intra_gateways[i] );			
      }
    }
		
  }
}

/*
  Neighbor Management Functions
*/
void
IDRM::sendBeacon() {
	
  if(IDRM_DEBUG)
    cout<<"sendbeacon :"<<myNodeId<<endl;
	
  if(!now_gateway)
    return;
	
  if(identity == false) return; //0404
	
  Packet *p = Packet::alloc();
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type = IDRMTYPE_BEACON;
	
  memcpy(rh->my_dst, my_dst, sizeof(bool)*NUM_DST);
  memcpy(rh->my_reachables, my_reachables, sizeof(bool)*NUM_DST);//cs218_idrm2
	
	
  // Update my current estimate to the various destinations
  for(int i=0;i<n_idrm_rt_entry;i++)
    if(idrm_rt_table[i].dst >= 0 && idrm_rt_table[i].dst < NUM_DST && idrm_rt_table[i].route_type != 'D')
      {
	if((idrm_rt_table[i].hop_count+1) < my_hops[idrm_rt_table[i].dst])
	  my_hops[idrm_rt_table[i].dst] = (idrm_rt_table[i].hop_count+1);
	if((idrm_rt_table[i].hop_count) < hops_from_eIDRM[idrm_rt_table[i].dst])
	  hops_from_eIDRM[idrm_rt_table[i].dst] = (idrm_rt_table[i].hop_count);
      }
	
  memcpy(rh->my_hops, my_hops, sizeof(int)*NUM_DST);
	
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  ch->size()		= IP_HDR_LEN + sizeof(hdr_idrm);
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type()	= NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			// AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = IP_BROADCAST;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	 = 1;
	
  if(IDRM_DEBUG) {
    printf("IDRM_DEBUG SENDING BEACON pkt_hdr(src=%d dst=%d id=%d if_id %d) from node %d at %lf on if_id %d\n", ih->saddr(), ih->daddr(), 
	   ch->uid(), ch->iface(), myNodeId, CURR_TIME, myIfaceId);
    printf("IDRM_DEBUG SIZEOF HEADER: %d", sizeof(hdr_idrm));
  }
	
  // To transmit can schedule it a bit later or send immediately. Both the following options available. Uncomment exactly one of those
  //Scheduler::instance().schedule(target_, p, IDRM_JITTER);
	
  target_->recv(p, (Handler*) 0);
	
  if(now_gateway){
    n_out_beacon ++;
    s_out_beacon += ch->size();
  }
}

// Mission Policy
void			
IDRM::handle_beacon(nsaddr_t neighbour_id){
	
  if(IDRM_DEBUG)
    cout<<"handle_beacon :"<<myNodeId<<endl;
	
  int i;
  int new_g	= 1;
	
  // A beacon is received from neighbour_id
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG HANDLING BEACON %d -> %d at %lf\n", neighbour_id, myNodeId, CURR_TIME);
	
  // Add neighbour gateway to route table
  // Mission Policy
  for(i=0;i<(n_idrm_rt_entry);i++) {
		
    if( idrm_rt_table[i].dst == neighbour_id ) {
      if( idrm_rt_table[i].hop_count != 1 ) {
				
	//memset(idrm_rt_table[i].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	for(int a=0; a<MAX_HOP; a++){
	  idrm_rt_table[i].path[a] = -1;	
	}
				
	idrm_rt_table[i].path[0]   = neighbour_id;
	idrm_rt_table[i].next_hop  = neighbour_id;
	idrm_rt_table[i].hop_count = 1;
	idrm_rt_table[i].recv_t	   = CURR_TIME;
      }
			
      total_upd_routes++; 
			
      idrm_rt_table[i].route_type= 'U';
      idrm_rt_table[i].expire_t = CURR_TIME + IDRM_ROUTE_EXPIRY;
      new_g = 0;
      break;
    }
  }
	
  // Add the new neighbour to the route table
  // Mission Policy 
  // Hard Coded Untrusted Domain
	
  if( (new_g == 1) ) {
    if(IDRM_DEBUG)
      printf("\nIDRM_DEBUG HANDLING BEACON at node %d from node %d at %lf: found new neighbour %d \n",
	     myNodeId, neighbour_id, CURR_TIME, neighbour_id);
    idrm_rt_table[n_idrm_rt_entry].route_type= 'N';
    idrm_rt_table[n_idrm_rt_entry].dst		 = neighbour_id;
		
    //memset(idrm_rt_table[n_idrm_rt_entry].path, -1, sizeof(nsaddr_t)*MAX_HOP);
    for(int a=0; a<MAX_HOP; a++){
      idrm_rt_table[n_idrm_rt_entry].path[a] = -1;	
    }
		
    idrm_rt_table[n_idrm_rt_entry].path[0]	 = neighbour_id;
    idrm_rt_table[n_idrm_rt_entry].next_hop	 = neighbour_id;
    idrm_rt_table[n_idrm_rt_entry].hop_count = 1;
    idrm_rt_table[n_idrm_rt_entry].recv_t	 = CURR_TIME;
    idrm_rt_table[n_idrm_rt_entry].expire_t	 = CURR_TIME + IDRM_ROUTE_EXPIRY;
		
    //0718: 'N' event happen
    idrm_rt_table[n_idrm_rt_entry].updateId	 = updateId;
    updateId++;
		
    if(IDRM_DEBUG)
      printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (from eIDRM Beacon)\n",
	     myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	     idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
		
    n_idrm_rt_entry++;
    total_new_routes++;
  }
  dumpRT();
	
  // Mission Policy
  if( (new_g == 1) /*&& !((Neighbor_Domain == 'B') && Policy && (Domain == 'A'))*/) {
    sendRouteTableRequest( neighbour_id );
  }
}

void
IDRM::recvBeacon(Packet *p) {
	
  if(IDRM_DEBUG)
    cout<<"recvbeacon :"<<myNodeId<<endl;
	
  //0301;
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  // If the packet is from the same domain discard
  if(!from_diff_domain(ih->saddr())){
    Packet::free(p);
    return;
  }
	
  //0316
  //cs218_idrm2
  if (!inter_nodes[ih->saddr()])	// remember which eIDRM node sent me beacon
    inter_nodes[ih->saddr()] = 1;
  //
	
  int index = getIndex(ih->saddr());
	
  if( !inter_neighbors[index]){
    inter_neighbors[index] = true;
    inter_neighbors_counter++;
  }
	
  if(IDRM_DEBUG)
    printf("Recv BEacon from %d to %d \n",ih->saddr(),myNodeId);
	
  dst_from_eIDRM[ih->saddr()]	 = 1;
  hops_from_eIDRM[ih->saddr()] = 1;
	
  if(IDRM_DEBUG){
    cout<<"Before "<<endl;
    for(int i=0; i<NUM_DST; i++){	
      printf("(%d,%d) ",i,hops_from_eIDRM[i]);
    }	
  }
  
  for(int i=0; i<NUM_DST; i++){	
    dst_from_eIDRM[i] = dst_from_eIDRM[i] | rh->my_dst[i];
    if (i/num_domain_members!=myNodeId/num_domain_members) {
    
			my_reachables[i] = my_reachables[i] | (int)rh->my_reachables[i]; //cs218_idrm2 update recv dst array to existing one
    }
	  
    if(rh->my_hops[i] < hops_from_eIDRM[i])
      hops_from_eIDRM[i] = rh->my_hops[i];
	  
    if(IDRM_DEBUG)
      printf("(%d,%d) ",dst_from_eIDRM[i],rh->my_hops[i]);
  }
  if(IDRM_DEBUG)
    printf("\n");
	
	
	
  // Update my current estimate to the various destinations
  for(int i=0;i<n_idrm_rt_entry;i++)
    if(idrm_rt_table[i].dst >= 0 && idrm_rt_table[i].dst < NUM_DST && idrm_rt_table[i].route_type != 'D')
      {
	if(idrm_rt_table[i].hop_count < hops_from_eIDRM[idrm_rt_table[i].dst])
	  hops_from_eIDRM[idrm_rt_table[i].dst] = idrm_rt_table[i].hop_count;
      }
	
	
  if(IDRM_DEBUG){
    cout<<endl<<"After "<<endl;
    for(int i=0; i<NUM_DST; i++){	
      printf("(%d,%d) ",i,hops_from_eIDRM[i]);
    }
  }
	
  //struct hdr_idrm *rh = HDR_IDRM(p);
	
  n_in_beacon++;
  s_in_beacon += ch->size();
	
  if(IDRM_DEBUG)
    printf("\n");
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG RECEIVING BEACON pkt_hdr(src=%d dst=%d id=%d if_id %d) on node %d at %lf on if_id %d\n", ih->saddr(), ih->daddr(), ch->uid(), ch->iface(), myNodeId, CURR_TIME, myIfaceId);
	
  //Beacon is from different Domain or the same domain
  // Fill the neighbour list code for IDRM gateways here
  // This feature to be added
	
  for(int i=0; i<num_intra_gateway; i++){
    if( intra_gateways[i] == ih->saddr() ) //update timestamp
      intra_gateways_ts[i] = CURR_TIME +	IDRM_INTRA_GATEWAY_EXPIRY;
  }
	
  if(now_gateway)
    handle_beacon(ih->saddr());		
  // Finally consume and destroy the packet
  Packet::free(p);
}

void
IDRM::sendRouteTableRequest(nsaddr_t dst) {
	
  if(IDRM_DEBUG)
    cout<<"sendroutetablerequest :"<<myNodeId<<endl;
	
  if(!now_gateway)
    return;
  if(identity == false) return; //0404
	
  Packet *p			= Packet::alloc();
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type = IDRMTYPE_ROUTE_TABLE_REQUEST;
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  ch->size()		= IP_HDR_LEN + sizeof(hdr_idrm);
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	= MAX_HOP;
	
  if(IDRM_DEBUG)	
    printf("IDRM_DEBUG SENDING RT REQUEST from node %d to node %d at %lf \n", 
	   myNodeId, dst, CURR_TIME);
	
  if(now_gateway){
    n_out_request ++;
    s_out_request += ch->size();
  }
	
  target_->recv(p, (Handler*) 0);
}

void
IDRM::recvRouteTableRequest(Packet *p) {
	
  if(IDRM_DEBUG)
    cout<<"recvroutetablerequest :"<<myNodeId<<endl;
	
  if(!now_gateway)
    {
      Packet::free(p);
      return;
    }
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_cmn *ch	= HDR_CMN(p);
	
  n_in_request++;
  s_in_request += ch->size();
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG RECV RT REQUEST	(src=%d dst =%d) at node %d at %lf \n", 
	   ih->saddr(), ih->daddr(), myNodeId, CURR_TIME);
  sendRouteTable(IP_BROADCAST, 0);
  Packet::free(p);
}

void
IDRM::sendRouteTable(nsaddr_t dst, int start_index) {
	
  if(IDRM_DEBUG)
    cout<<"sendroutetable :"<<myNodeId<<"Active "<<now_gateway<<endl;
	
  if(!now_gateway)
    return;
  if(identity == false) return; //0404
	
  if( n_idrm_rt_entry == 0 ) return;
	
  int i,j,c;
  //Packet *p			  = Packet::alloc();
  Packet *p = Packet::alloc(IDRM_PACKET_SIZE);	//payload is routing info.
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  //ch->size()	  = IP_HDR_LEN + sizDR_LEN + sizeof(hdr_idrm) + IDRM_PACKET_SIZE; //IP hdr + IDRM hdr + payload
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	= MAX_HOP;
	
  rh->ih_type = IDRMTYPE_ROUTE_TABLE_REPLY;
	
	
  //idrm_rt_entry* tmp = (idrm_rt_entry*) ((PacketData*)p->data_)->data();
	
  c=0;
	
  //char* buf = new char[ sizeof(idrm_rt_entry) ];
  //idrm_rt_entry* idrm_payload = (idrm_rt_entry*)buf;
  idrm_rt_entry* idrm_payload = (idrm_rt_entry*)my_idrm_payload;
	
  for(i=start_index; i<n_idrm_rt_entry;i++){
		
		
    //idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->data_)->data() + (sizeof(idrm_rt_entry)*c));
    idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->userdata())->data() + (sizeof(idrm_rt_entry)*c));
		
    //strcpy(idrm_payload->MANET_ID, idrm_rt_table[i].MANET_ID);
    idrm_payload->route_type			= 'R';
    idrm_payload->dst						= idrm_rt_table[i].dst;
    idrm_payload->hop_count			= idrm_rt_table[i].hop_count;
		
    // Mission Policy
    for(j=0; j<idrm_rt_table[i].hop_count; j++){
      if(j >= MAX_HOP){break;} //0320
      idrm_payload->path[j]		= idrm_rt_table[i].path[j];
			
    }
		
    //memcpy((((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * c) , buf, sizeof(idrm_rt_entry));
    //memcpy(tmp, buf, sizeof(idrm_rt_entry));
		
    memcpy(tmp, my_idrm_payload, sizeof(idrm_rt_entry)); //0902
		
		
    c++;
		
    if( c == MAX_ROUTE_PER_PACKET )
      break;
  }
	
  //if(buf != NULL) delete []buf;
	
  /*	
  //for debugging...
  printf("[IDRM_SH] NodeId:%d print IDRM_RT payload\n", myNodeId);
  for(i=0; i<c;i++){
	 
  tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i);
	 
  printf("	dst:%d, route_type:%c, hop_count:%d path[0]=:%d \n", 
  tmp->dst, tmp->route_type, tmp->hop_count, tmp->path[0]);
  }
  */
	
  if( c == 0 ){
    Packet::free(p);
    return; //no entry to send
  }
	
  rh->n_route = c;
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm) + ( sizeof(idrm_rt_entry) * c); // actual packet size
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG SENDING R TABLE from node %d to node %d at %lf \n", myNodeId, ih->daddr(), CURR_TIME);
	
  target_->recv(p, (Handler*) 0);
	
  if(now_gateway){	
    n_out_table ++;
    s_out_table += ch->size();
  }
	
  if( start_index + c < n_idrm_rt_entry ) {
    sendRouteTable(dst, start_index + c);
  }
	
  //if(!buf) delete buf;
	
}

void
IDRM::sendRouteUpdate(int flag) {
	
  if(IDRM_DEBUG)
    cout<<"sendrouteupdate :"<<myNodeId<<endl;
	
  if(!now_gateway)
    return;
  if(identity == false) return; //0404
	
  if( n_idrm_rt_entry	 == 0 ) return;
	
  //if(IDRM_DEBUG)
  //	printf("NodeId:%d, flag:%d IDRM_PACKET_SIZE:%d\n", myNodeId, flag,IDRM_PACKET_SIZE);
	
  //Packet *p			  = Packet::alloc();
  Packet *p = Packet::alloc(IDRM_PACKET_SIZE);
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  int i,j,r;
	
  rh->ih_type = IDRMTYPE_ROUTE_UPDATE;
	
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  //ch->size()	  = IP_HDR_LEN + sizeof(hdr_idrm) + IDRM_PACKET_SIZE; //IP hdr + IDRM hdr + payload
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = IP_BROADCAST;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	= MAX_HOP;
	
  //Payload Setup: 0614
  //idrm_rt_entry* tmp = (idrm_rt_entry*) ((PacketData*)p->data_)->data();
	
  //char* buf = new char[ sizeof(idrm_rt_entry) ];
  //idrm_rt_entry* idrm_payload = (idrm_rt_entry*)buf;
  idrm_rt_entry* idrm_payload = (idrm_rt_entry*)my_idrm_payload;
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG SENDING R Update from node %d at %lf\n", myNodeId, CURR_TIME);
  if(IDRM_DEBUG)
    printf("Before RT update send\n");
  dumpRT();
  for(i=0, r=0;i<n_idrm_rt_entry && r <MAX_ROUTE_PER_PACKET;i++) {
    if(		idrm_rt_table[i].route_type == 'N' || 
		idrm_rt_table[i].route_type == 'D' ||
		idrm_rt_table[i].route_type == 'U' ) { //not normal
			
			
      //strcpy( idrm_payload->MANET_ID, idrm_rt_table[i].MANET_ID);
      idrm_payload->route_type			= idrm_rt_table[i].route_type;
      idrm_payload->dst							= idrm_rt_table[i].dst;
      idrm_payload->hop_count				=	idrm_rt_table[i].hop_count;
      //	printf("Node %d sending path\n",myNodeId);
      // Mission Policy
      for(j=0;j<idrm_rt_table[i].hop_count;j++) {
	if(j >= MAX_HOP){break;} //0320
	idrm_payload->path[j]			= idrm_rt_table[i].path[j];
				
				
      }
			
      //0718
      if(idrm_rt_table[i].route_type == 'N' || idrm_rt_table[i].route_type == 'D')
	idrm_payload->updateId		= idrm_rt_table[i].updateId;
			
      //idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->data_)->data() + (sizeof(idrm_rt_entry)*r));
      idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->userdata())->data() + (sizeof(idrm_rt_entry)*r));			
      //memcpy(tmp, buf, sizeof(idrm_rt_entry));
      memcpy(tmp, my_idrm_payload, sizeof(idrm_rt_entry));
			
      //only change entries updated by IIDRM when this function is called by IIDRM
      if(flag == IIDRM && intra_updated_entry_lookup(idrm_rt_table[i].dst)){
	if( idrm_rt_table[i].route_type != 'D' ) {
	  idrm_rt_table[i].route_type = 'R';
	}
	else {
	  idrm_rt_table[i].route_type = 'T';
	}
      }
      else if(flag == EIDRM){ //called by EIDRM, do normal processing..
	if( idrm_rt_table[i].route_type != 'D' ) {
	  idrm_rt_table[i].route_type = 'R';
	}
	else {
	  idrm_rt_table[i].route_type = 'T';
	}
      }
			
      r++;
    }
  }
	
  rh->n_route = r;
	
  for(i=0;i<n_idrm_rt_entry;i++) {
    if( idrm_rt_table[i].route_type == 'T' ) {
			
      if (strcmp(baseRAgentName, "AODV") == 0){
	aodv_rt_entry *rt;
	rt = ((AODV *)baseRAgent)->rtable.rt_lookup( idrm_rt_table[i].dst );
	if(rt){
	  ((AODV *)baseRAgent)->rt_down(rt);
	}
      }
    }
  }
	
  // Delete all D route
  for(i=0;i<n_idrm_rt_entry;i++) {
    if( idrm_rt_table[i].route_type == 'T' ) {
      remove_route(i);
      i = -1;
    }
  }
	
  if(IDRM_DEBUG)
    printf("After RT update send\n");
  dumpRT();
	
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm) + ( sizeof(idrm_rt_entry) * r); // actual packet size
	
  if( r > 0 )	 {
    if(IDRM_DEBUG)
      printf("IDRM_DEBUG SENDING R Update from node %d r:%d at %lf \n", myNodeId, r, CURR_TIME);
		
    if(IDRM_DEBUG)
      printf("[IDRM_SH] NodeId:%d print IDRM_RT r:%d payload\n", myNodeId, r);
		
    for(i=0; i<r;i++){
      //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i);
      //idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i);
      idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i);
      if(IDRM_DEBUG)
	printf("dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
      // Mission Policy
      for(j=0;j<tmp->hop_count;j++) {
	if(j >= MAX_HOP){break;} //0320
				
	if(IDRM_DEBUG)
	  printf("%d ,",tmp->path[j]);
      }
			
    }
		
    target_->recv(p, (Handler*) 0);
		
    if(now_gateway){
      n_out_update ++;
      s_out_update += ch->size();
    }
  }
  else{
    Packet::free(p);	 //no route to send
  }
	
  //if(!buf) delete buf;
	
  if( r == MAX_ROUTE_PER_PACKET ) {
    sendRouteUpdate(flag);
  }
}


void
IDRM::sendRouteUpdatebyIIDRM() {
  //
  if(IDRM_DEBUG) 
    cout<<"sendrouteupdatebyiidrm :"<<myNodeId<<endl;
	
  if(!now_gateway)
    {
      return;
    }
  if(identity == false) return; //0404
	
  if( intra_updated_counter  <= 0 ) return;
	
	
  //Packet *p			  = Packet::alloc();
  Packet *p = Packet::alloc(IDRM_PACKET_SIZE);
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  int i,j,r;
	
  rh->ih_type = IDRMTYPE_ROUTE_UPDATE;
	
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  //ch->size()	  = IP_HDR_LEN + dw + IDRM_PACKET_SIZE; //IP hdr + IDRM hdr + payload
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = IP_BROADCAST;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	= MAX_HOP;
	
  if(IDRM_DEBUG)
    printf("before sending update\n");
	
  dumpRT();
  //idrm_rt_entry* tmp = (idrm_rt_entry*) ((PacketData*)p->data_)->data();
	
  //char* buf = new char[ sizeof(idrm_rt_entry) ];
  //idrm_rt_entry* idrm_payload = (idrm_rt_entry*)buf;
  idrm_rt_entry* idrm_payload = (idrm_rt_entry*)my_idrm_payload;
	
  for(i=0, r=0;i<intra_updated_counter && r <MAX_ROUTE_PER_PACKET;i++) {
		
    idrm_rt_entry* rt = rt_lookup(intra_updated_entry[i]);
    if(rt == NULL) continue;
		
    if(		rt->route_type == 'N' || 
		rt->route_type == 'D' ||
		rt->route_type == 'U' ) { //not normal
			
      //strcpy( idrm_payload->MANET_ID, rt->MANET_ID);
      idrm_payload->route_type			= rt->route_type;
      idrm_payload->dst							= rt->dst;
      idrm_payload->hop_count				=	rt->hop_count;
			
      for(j=0;j<rt->hop_count;j++) {
	if(j >= MAX_HOP){break;} //0320
	idrm_payload->path[j]			= rt->path[j];
				
      }
			
      //0718
      if(rt->route_type == 'N' || rt->route_type == 'D')
	idrm_payload->updateId		= rt->updateId;
			
      //idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->data_)->data() + (sizeof(idrm_rt_entry)*r));
      idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->userdata())->data() + (sizeof(idrm_rt_entry)*r));
      //memcpy(tmp, buf, sizeof(idrm_rt_entry));
      memcpy(tmp, my_idrm_payload, sizeof(idrm_rt_entry));
			
      if( idrm_rt_table[i].route_type != 'D' ) {
	rt->route_type = 'R';
      }
      else {
	rt->route_type = 'T';
      }
			
      r++;
    }
  }
	
  //if(buf != NULL) delete []buf;
	
  //for debugging
  /*		
		printf("[IDRM_SH] NodeId:%d print IDRM_RT r:%d payload\n", myNodeId, r);
		for(i=0; i<r;i++){
		tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i);
		printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
		}
  */
	
  //rh->n_route = r+1;
  rh->n_route = r;
	
  for(i=0;i<n_idrm_rt_entry;i++) {
    if( idrm_rt_table[i].route_type == 'T' ) {
			
      if (strcmp(baseRAgentName, "AODV") == 0){
	aodv_rt_entry *rt;
	rt = ((AODV *)baseRAgent)->rtable.rt_lookup( idrm_rt_table[i].dst );
	if(rt){
	  ((AODV *)baseRAgent)->rt_down(rt);
	}
      }
    }
  }
	
  // Delete all D route
  for(i=0;i<n_idrm_rt_entry;i++) {
    if( idrm_rt_table[i].route_type == 'T' ) {
      remove_route(i);
      i = -1;
    }
  }
  if(IDRM_DEBUG)
    printf("after sending update\n");
  dumpRT();
	
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm) + ( sizeof(idrm_rt_entry) * r); // actual packet size
	
  //printf("p2 id: %d intra_counter:%d r: %d\n", myNodeId, intra_updated_counter, r);
	
  if( r > 0 )	 {
    if(IDRM_DEBUG)
      printf("IDRM_DEBUG SENDING R Update from node %d r:%d at %lf \n", myNodeId, r, CURR_TIME);
		
    if(IDRM_DEBUG)
      printf("[IDRM_SH] NodeId:%d print IDRM_RT r:%d payload\n", myNodeId, r);
		
    for(i=0; i<r;i++){
      //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i);
      //idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i);
      idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i);
      if(IDRM_DEBUG)			
	printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
    }
		
    target_->recv(p, (Handler*) 0);
		
    if(now_gateway){
      n_out_update ++;
      s_out_update += ch->size();
    }
		
    intra_updated_counter -=r;
		
  }
  else{
    Packet::free(p);	
  }
	
  //if(!buf)	delete buf;
	
  if( r == MAX_ROUTE_PER_PACKET ) {
    sendRouteUpdatebyIIDRM();
  }
	
}

void
IDRM::recvRouteUpdate(Packet *p) {
	
  if(IDRM_DEBUG)
    cout<<"recvrouteupdate :"<<myNodeId<<endl;
	
  if(!now_gateway)
    {
      Packet::free(p);
      return;
    }
  int i,j,r;
	
  int new_r, loop;
	
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  n_in_update ++;
  s_in_update += ch->size();
	
  nsaddr_t pkt_src = ih->saddr();
	
	
  //handle_beacon(pkt_src); //added 0614: overheard msg...
  if(IDRM_DEBUG)	
    printf("IDRM_DEBUG RECV RT Update, %d->%d time %.4lf:\n", pkt_src, myNodeId, CURR_TIME);
  dumpPacket(p);
  dumpRT();
	
  //IDRM_SH: 0614 look at payload...
  //idrm_rt_entry* idrm_payload;
  //idrm_payload = (idrm_rt_entry**)((PacketData*)p->data_)->data_;
	
  if(IDRM_DEBUG)	{
    printf("[IDRM_SH] NodeId:%d print IDRM_RT in RTUpdate r:%d payload\n", myNodeId, r);
    for(i=0; i<rh->n_route;i++){		
      //idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i);
      idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i);
      printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
    }
  }
	
  for(i=0;i<rh->n_route;i++) {
		
    //idrm_rt_entry* idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i));
    idrm_rt_entry* idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i));		
		
    if( idrm_payload->dst == myNodeId ) continue; //added
		
    // Others Remove route
    if( idrm_payload->route_type == 'D'){
			
      for(j=0;j<n_idrm_rt_entry;j++)	{
	if( idrm_rt_table[j].next_hop == pkt_src && idrm_rt_table[j].dst == idrm_payload->dst){
	  idrm_rt_table[j].route_type = 'D';
					
	  if (strcmp(baseRAgentName, "AODV") == 0)
	    ((AODV *)baseRAgent)->rtable.rt_delete( idrm_rt_table[j].dst ); //0623
					
	  //0718: 'D' event propagated!
	  idrm_rt_table[j].updateId	 = idrm_payload->updateId;
					
	  /*					
						printf("[IDRM_SH_Update Rec] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvRouteUpdate eIDRM)\n",
						myNodeId, idrm_rt_table[j].route_type, idrm_rt_table[j].updateId, 
						idrm_rt_table[j].dst, CURR_TIME);
	  */
	}
      }
    }
		
    // Others Add route
    if( idrm_payload->route_type == 'N' || idrm_payload->route_type == 'U'){
			
      new_r = 1;
      loop = 0;			// check loop
			
			
			
      for(r=0; r<idrm_payload->hop_count; r++){
	if( idrm_payload->path[r] == myNodeId){
	  loop = 1;
	  break;
	}
				
				
				
				
				
      }
			
      if (loop ) 
	continue;
			
      for(j=0;j<n_idrm_rt_entry;j++){
	if( idrm_rt_table[j].dst == idrm_payload->dst){
					
	  new_r = 0;
					
	  // have existing route to dst, check hop_count is smaller or not
	  if( idrm_payload->hop_count < idrm_rt_table[j].hop_count-1){	//0723
						
	    //before path update, initialize
	    //memset(idrm_rt_table[j].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	    for(int a=0; a< MAX_HOP; a++){
	      idrm_rt_table[j].path[a] = -1;	
	    }
						
	    idrm_rt_table[j].path[0]   = ih->saddr();
						
	    for(r=0; r<idrm_payload->hop_count; r++){
	      if(r >= MAX_HOP-1){break;} //0320
	      idrm_rt_table[j].path[r+1]	= idrm_payload->path[r];
							
	    }
						
	    idrm_rt_table[j].next_hop  = ih->saddr();
	    idrm_rt_table[j].hop_count = idrm_payload->hop_count + 1;
						
	    idrm_rt_table[j].route_type = 'U';
	    idrm_rt_table[j].recv_t	   = CURR_TIME;
	    idrm_rt_table[j].expire_t  = CURR_TIME + IDRM_ROUTE_EXPIRY;
	  }//changed 0625
	  else if( (idrm_payload->hop_count == (idrm_rt_table[j].hop_count-1)) &&
		   (ih->saddr() == idrm_rt_table[j].next_hop)){	//0723:for the same route..update expire timer
	    //(idrm_payload->next_hop == idrm_rt_table[j].next_hop)){	//0723:for the same route..update expire timer
						
	    idrm_rt_table[j].route_type			= 'U';				
	    idrm_rt_table[j].recv_t					= CURR_TIME;
	    idrm_rt_table[j].expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;
	  }
	}
      }
			
      if( new_r ) {
				
	idrm_rt_table[n_idrm_rt_entry].route_type	= 'N';
	idrm_rt_table[n_idrm_rt_entry].dst			= idrm_payload->dst;
				
	//memset(idrm_rt_table[n_idrm_rt_entry].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	for(int a=0; a<MAX_HOP; a++){
	  idrm_rt_table[n_idrm_rt_entry].path[a] = -1;	
	}
				
	idrm_rt_table[n_idrm_rt_entry].path[0]		= ih->saddr();
				
				
	if(IDRM_DEBUG)	printf("!!!\n");
				
	for(r=0; r<idrm_payload->hop_count; r++){
	  if(r >= MAX_HOP-1){break;} //0320
	  idrm_rt_table[n_idrm_rt_entry].path[r+1]   = idrm_payload->path[r];
					
	  if(IDRM_DEBUG)
	    printf("(%d), ",idrm_payload->path[r]);
					
	}
				
	idrm_rt_table[n_idrm_rt_entry].next_hop	 = ih->saddr();
	idrm_rt_table[n_idrm_rt_entry].hop_count = idrm_payload->hop_count + 1;
	idrm_rt_table[n_idrm_rt_entry].recv_t	 = CURR_TIME;
	idrm_rt_table[n_idrm_rt_entry].expire_t	 = CURR_TIME + IDRM_ROUTE_EXPIRY;
				
	//0718: 'N' event propagated! // we only consider 'N'
	if(idrm_payload->route_type == 'N'){
	  idrm_rt_table[n_idrm_rt_entry].updateId	 = idrm_payload->updateId;
					
	  /*
	    printf("[IDRM_SH_Update Rec] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvRouteUpdate eIDRM)\n",
	    myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	    idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
	  */
	}
	else{
	  //propated from me: it was U but it is new for me
	  idrm_rt_table[n_idrm_rt_entry].updateId	 = updateId;
	  updateId++;					
					
	  /*
	    printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvRouteUpdate eIDRM)\n",
	    myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	    idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
	  */
	}
				
	n_idrm_rt_entry++;
      }
    }
  }
	
  Packet::free(p);
	
  if(IDRM_DEBUG)	
    printf("IDRM_DEBUG AFTER RECV RT Update, %d->%d time %.4lf:\n", pkt_src, myNodeId, CURR_TIME);
  dumpRT();
	
  // Should send out route update for fast convergence
}

void
IDRM::recvRouteTable(Packet *p) {
	
  if(IDRM_DEBUG)
    cout<<"recvroutetable :"<<myNodeId<<endl;
	
  if(!now_gateway)
    {
      Packet::free(p);
      return;
    }
  int i,j;
  int r;
  int new_r;
  int loop	  = 0;
	
  //int exist_dst = 0;
	
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  n_in_table ++;
  s_in_table += ch->size();
	
	
	
  if(IDRM_DEBUG)
    {	
      printf("IDRM_DEBUG RECV RT (src=%d dst =%d) at node %d at %lf n_rt = %d\n",
	     ih->saddr(), ih->daddr(), myNodeId, CURR_TIME, rh->n_route);
      printf("IDRM_DEBUG Before RECV RT PACKET, node %d RT is :\n", myNodeId);
      dumpRT();
    }
  //IDRM_SH: 0614 look at payload...
  //idrm_rt_entry** idrm_payload;
  //idrm_payload = (idrm_rt_entry**)((PacketData*)p->data_)->data_;
  //idrm_rt_entry* idrm_payload;
  //handle_beacon(ih->saddr()); //added 0614: overheard msg...
	
  if(IDRM_DEBUG)
    printf("Source addr : %d\n",ih->saddr());
	
  for(i=0; i<rh->n_route; i++){
		
    //idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i));
    //idrm_rt_entry* idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i));
    idrm_rt_entry* idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i));			
		
    if( idrm_payload->dst == myNodeId ) continue; //added
		
    //printf("[IDRM_SH] NodeId:%d i=%d dst:%d\n", myNodeId, i, idrm_payload->dst);
		
    new_r = 1;
    loop = 0;
		
		
    for(r=0; r< idrm_payload->hop_count; r++){
      if(r >= MAX_HOP){break;} //0320
      if( idrm_payload->path[r] == myNodeId){
	loop = 1;
	break;	
      }
    }
		
    if(loop )
      continue;
		
    for(j=0; j<n_idrm_rt_entry; j++){
      if( idrm_rt_table[j].dst == idrm_payload->dst ){
	new_r = 0;
				
	if(IDRM_DEBUG)
	  printf("Update Entered\n");
	// have existing route to dst, check hop_count is smaller or not
	if( idrm_payload->hop_count < (idrm_rt_table[j].hop_count - 1) ){ //0702
					
	  idrm_rt_table[j].route_type			= 'U';
	  //before path update, initialize
	  //memset(idrm_rt_table[j].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	  for(int a=0; a<MAX_HOP; a++){
	    idrm_rt_table[j].path[a] = -1;	
	  }
					
	  idrm_rt_table[j].path[0]				= ih->saddr();
					
					
	  for(r=0; r<idrm_payload->hop_count; r++){
	    if(r >= MAX_HOP-1){break;} //0320
	    idrm_rt_table[j].path[r+1]		=	idrm_payload->path[r]; //
						
	  }
					
	  idrm_rt_table[j].next_hop				=	ih->saddr();
	  idrm_rt_table[j].hop_count			=	idrm_payload->hop_count + 1;
	  idrm_rt_table[j].recv_t					= CURR_TIME;
	  idrm_rt_table[j].expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;
					
	}
	else if( (idrm_payload->hop_count == (idrm_rt_table[j].hop_count-1)) && 
		 (ih->saddr() == idrm_rt_table[j].next_hop)){	//0723:for the same route..update expire timer
	  //(idrm_payload->next_hop == idrm_rt_table[j].next_hop)){	//0723:for the same route..update expire timer
					
	  idrm_rt_table[j].route_type			= 'U';				
	  idrm_rt_table[j].recv_t					= CURR_TIME;
	  idrm_rt_table[j].expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;
	}
      }
    }
		
    // Find new route, add to route table
    if( new_r == 1 ){
			
      if(IDRM_DEBUG)
	printf("entered new route: node: %d\n",ih->saddr());
			
      idrm_rt_table[n_idrm_rt_entry].route_type				= 'N';
      idrm_rt_table[n_idrm_rt_entry].dst							= idrm_payload->dst;
			
      //memset(idrm_rt_table[n_idrm_rt_entry].path, -1,sizeof(nsaddr_t)*MAX_HOP);
      for(int a=0; a<MAX_HOP; a++){
	idrm_rt_table[n_idrm_rt_entry].path[a] = -1;
      }
			
      idrm_rt_table[n_idrm_rt_entry].path[0]					= ih->saddr();
			
			
      for(r=0; r< idrm_payload->hop_count; r++){
	if(r >= MAX_HOP-1){break;} //0320
	idrm_rt_table[n_idrm_rt_entry].path[r+1]			=	idrm_payload->path[r];	
				
      }
			
      idrm_rt_table[n_idrm_rt_entry].next_hop					= ih->saddr();
      idrm_rt_table[n_idrm_rt_entry].hop_count				= idrm_payload->hop_count+1;
      idrm_rt_table[n_idrm_rt_entry].recv_t						= CURR_TIME;
      idrm_rt_table[n_idrm_rt_entry].expire_t					=	CURR_TIME + IDRM_ROUTE_EXPIRY;
			
      //0718: 'N' event happen
      idrm_rt_table[n_idrm_rt_entry].updateId	 = updateId;
      updateId++;
			
      if(IDRM_DEBUG)	
	printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (from recvRouteTable)\n",
	       myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	       idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
      n_idrm_rt_entry++;
    }
  }
	
  Packet::free(p);
	
  if(IDRM_DEBUG)	
    printf("IDRM_DEBUG After RECV RT PACKET, node %d RT is :\n", myNodeId);
  dumpRT();
}
// eIDRM
// ======================================================================

// ======================================================================
// iIDRM
void
IDRM::sendIntraBeacon(nsaddr_t dst) {
	
  if(IDRM_DEBUG)
    cout<<"sendintrabeacon :"<<myNodeId<<" Active: "<<now_gateway<<endl;
	
  if(IDRM_DEBUG)
    printf("IntraBeacon called NodeId: %d  dst: %d, identity = %d at %lf\n",myNodeId,dst, identity, CURR_TIME);
	
  if(identity == false) return; //0404
	
  if( dst == -1 ) return; //invalid address
	
  Packet *p = Packet::alloc();
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type = IDRMTYPE_INTRA_BEACON;
	
	
	
	
  //0214
  int index = getIndex(myNodeId);
  rh->routes = intra_neighbors_routes[index];
  rh->AvgHop = intra_neighbors_hops[index];
	
  memcpy(rh->my_reachables, my_reachables, sizeof(bool)*NUM_DST);//cs218_idrm2

	
	
  //printf("0316 NodeId: %d(%d) sendIIDRM to %d at %lf\n", myNodeId, rh->routes, dst, CURR_TIME);
	
  //memcpy(rh->MANET_ID, MANET_ID, sizeof(char) * 32 ); //IDRM_SH: put my MANET ID
	
  ch->uid() = beaconId++;
  ch->ptype() = PT_IDRM;
	
  if(IDRM_DEBUG)
    printf("PT_IDRM set in IDRM = %d and ch->ptype = %d",PT_IDRM,ch->ptype());
	
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm);
  ch->iface() = -2;
  ch->error() = 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_ = myNodeId;		   // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
	
  if(IDRM_DEBUG)
    printf("Node %d sending intra beacon to Dest : %d",myNodeId,dst);
	
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_ = NETWORK_DIAMETER;
	
  //printf("[IDRM_SH] NodeId:%d sending Intra Beacon() to %d at %lf\n ", myNodeId, ih->daddr(), CURR_TIME); 
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to AODV] NodeId:%d sent Intra Beacon to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to DSDV] NodeId:%d (%d) sent Intra Beacon to %d at %lf\n", myNodeId, intra_neighbors_routes[index], dst, CURR_TIME);
    ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
  }else{
    Packet::free(p);
  }
	
  if(now_gateway){	
    n_out_intra_beacon ++;
    s_out_intra_beacon += ch->size();
  }
}

void
IDRM::recvIntraBeacon(Packet *p) {
  /*todo:
    1)check it is new, if new add into intra_idrm_rt_table
    2)update ts
    3)sendIntraRTRequest
  */
	
  if(IDRM_DEBUG)
    cout<<"recvintrabeacon :"<<myNodeId<<endl;
  //always receive intra beacon
	
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  n_in_intra_beacon++;
  s_in_intra_beacon += ch->size();
	
  if(IDRM_DEBUG)	
    printf("[IDRM_SH] NodeId:%d receiving Intra Beacon() from %d iface:%d at %lf\n", myNodeId, ih->saddr(), ch->iface(), CURR_TIME); 
	
  bool newGateway = false;
	
  for(int i=0; i<num_intra_gateway; i++){
    if( intra_gateways[i] == ih->saddr() ){ //update timestamp
      if( intra_gateways_ts[i] < CURR_TIME )
	newGateway = true;
      intra_gateways_ts[i] = CURR_TIME +	IDRM_INTRA_GATEWAY_EXPIRY;
      if(IDRM_DEBUG)	
	printf("nodeid:%d dst:%d ts updated, info:%lf\n", myNodeId, ih->saddr(), intra_gateways_ts[i]);
    }
  }
	
  idrm_rt_entry* rt = intra_rt_lookup(ih->saddr());
  if(rt)
    rt->expire_t = CURR_TIME + IDRM_ROUTE_EXPIRY;
	
  int index = getIndex(ih->saddr());
	
  //0307
  if(intra_neighbors_ts[index] < 0){ //new intra neighbor
		
    intra_neighbors_ts[index] = CURR_TIME + intra_gateway_expire;  
    // Printing Routes
    intra_neighbors_routes[index] = rh->routes;
    new_gateway = true;
    if(IDRM_DEBUG_0303)
      printf("0303 NodeId: %d new_gateway comes %d(%d) at %lf\n", myNodeId, ih->saddr(), rh->routes, CURR_TIME);
  }
  else{	//old neighbor, update ts
    intra_neighbors_ts[index] = CURR_TIME + intra_gateway_expire;
    if(IDRM_DEBUG)
      printf("Hey The routes are:%d", rh->routes);
		
    intra_neighbors_routes[index] = rh->routes;
    if(IDRM_DEBUG_0303)
      printf("0303 NodeId: %d beacon comes %d(index: %d) ts: %lf at %lf\n", 
	     myNodeId, ih->saddr(), index, intra_neighbors_ts[index], CURR_TIME);
  }
	
  //cs218_idrm2
  if (!intra_nodes[getIndex(ih->saddr())])  // remember which iIDRM node sent me beacon
    intra_nodes[getIndex(ih->saddr())] = 1; // this is for beacon interval change
  //below is for election
  my_reachables[ih->saddr()] = 1; //add sender to my reachables
  my_reachables[myNodeId] = 1; //self always 1!

	int offset = getIndex(ih->saddr())*NUM_DST;
	
  for (int i = 0; i< NUM_DST; i++) {
  //apply newly received dsts into my own array segment in others_reachables
  others_reachables[offset+i] = others_reachables[offset+i] | (int)rh->my_reachables[i]; 
	}	
	
  if (rh->AvgHop > 0)
    intra_neighbors_hops[index] = rh->AvgHop;
	
  //0307
	
	
  if( newGateway ){ //if it was new, send RT request
    //let's see
    sendIntraRTRequest(ih->saddr());
  }
	
  Packet::free(p);
}

//0902
void
IDRM::sortingNeighbors(){
	
  for(int i=0; i< num_intra_neighbors; i++){
    sorted_intra_neighbors[i] = intra_neighbors[i];
  }
	
  sorted_intra_neighbors[num_intra_neighbors] = myNodeId;
  num_intra_neighbors++;
	
  for(int i=0; i< num_intra_neighbors; i++){
    for(int j=0; j< num_intra_neighbors; j++){
      if( sorted_intra_neighbors[i] < sorted_intra_neighbors[j] ){
	int tmp = sorted_intra_neighbors[i];
	sorted_intra_neighbors[i] = sorted_intra_neighbors[j];
	sorted_intra_neighbors[j] = tmp;	
      }	
    }
  }
}

void
IDRM::sortingGateways(){
	
}

void
IDRM::resetNeighbors(){
	
}

void
IDRM::makingTree(){
	
}

void
IDRM::pickCenter(){
	
  //center = intra_neighbors[0];
  center = sorted_intra_neighbors[0];
	
  if(IDRM_DEBUG_09)
    printf("NodeId: %d my center is %d\n", myNodeId, center);
	
}
//0902

//0214
void
IDRM::makingGatewayTree(){
	
}

void
IDRM::pickGatewayCenter(){
	
}

void
IDRM::gateway_selection_check(nsaddr_t src, int rt, bool* gts){
	
  //sorted_gateways, sorted_gateways_routes,num_dynamic_gateways_selected
	
  if(election_algorithm == ALGORITHM_STATIC) return; //static
	
  //check whether its from different partition
	
  bool same_partition = false;
  for(int i=0; i<NUM_DST; i++){
    if(gts[i] & intra_neighbors_after_selection[i]) same_partition = true;
  }
	
  if(same_partition == false) return; //do not kill gts from different partition
	
	
  //check if min is mine and same rt then compare nodeId
  int last_index = num_dynamic_gateways_selected -1;
  bool updated = false;
  int turn_off_dst;
	
  for(int i=0; i<num_dynamic_gateways_selected; i++){ //if src is expceted gt
    if(sorted_gateways_after_selection[i] == src) return;
  }
	
  //0409
  //new gateway
  num_detected_pts++; 
  int new_num_dynamic_gateways = (int) ceil ( double(num_detected_pts) * pgw / 100.0 );
	
  if(new_num_dynamic_gateways == 0) new_num_dynamic_gateways = 1;
	
  if(new_num_dynamic_gateways > num_dynamic_gateways_selected) return; //ok, keep it
  //0409
	
	
  if(rt > sorted_gateways_routes[last_index]){
    updated = true;
		
    turn_off_dst = sorted_gateways_after_selection[last_index];
    sorted_gateways_after_selection[last_index] = src;
    sorted_gateways_routes[last_index] = rt;
		
    sendIntraGatewayTrunOff(turn_off_dst);
  }
  else if(rt == sorted_gateways_routes[last_index]){ //then compare nodeId, highest id wins
    if(src > sorted_gateways_after_selection[last_index] ){
      updated = true;
			
      turn_off_dst = sorted_gateways_after_selection[last_index];
      sorted_gateways_after_selection[last_index] = src;
      sorted_gateways_routes[last_index] = rt;
			
      sendIntraGatewayTrunOff(turn_off_dst);
    }
    else{
      sendIntraGatewayTrunOff(src);
    }
  }
  else{
    //turn off!
    sendIntraGatewayTrunOff(src);
  }
	
  //sorting checking
	
  if(updated){ //sorting
    int tmp;
    for(int i=0; i<num_dynamic_gateways_selected; i++){
      for(int j=0; j<num_dynamic_gateways_selected; j++){
	if( sorted_gateways_routes[i] > sorted_gateways_routes[j]){ 
	  tmp = sorted_gateways_routes[j];
	  sorted_gateways_routes[j] = sorted_gateways_routes[i];
	  sorted_gateways_routes[i] = tmp;
					
	  tmp = sorted_gateways_after_selection[j];
	  sorted_gateways_after_selection[j] = sorted_gateways_after_selection[i];
	  sorted_gateways_after_selection[i] = tmp;
	}
	else if(sorted_gateways_routes[i] == sorted_gateways_routes[j]){ //if RT is the same, compare nodeId
	  if( sorted_gateways_after_selection[i] > sorted_gateways_after_selection[j]){
	    tmp = sorted_gateways_routes[j];
	    sorted_gateways_routes[j] = sorted_gateways_routes[i];
	    sorted_gateways_routes[i] = tmp;
						
	    tmp = sorted_gateways_after_selection[j];
	    sorted_gateways_after_selection[j] = sorted_gateways_after_selection[i];
	    sorted_gateways_after_selection[i] = tmp;
	  }
	}
      }	
    }
  }
	
  //sorting checking
}

//--- cs218_idrm1 ---//
void
IDRM::setGateway(){
  cout<<"setgateway :"<<myNodeId<<endl;
  if(election_algorithm == ALGORITHM_STATIC){	//static
    return; 
  }
	
  int index = getIndex(myNodeId);
  int offset = index*NUM_DST;
  int* the_sorted_gateways = new int[num_intra_gateway];
	
  memset(the_sorted_gateways, -1, sizeof(int)*num_intra_gateway);
  //memcpy(others_reachables+offset, my_reachables, sizeof(int)*NUM_DST);
  for (int i = 0; i< NUM_DST; i++) {
    others_reachables[offset+i] = (int)my_reachables[i];
  }
	
  findBestGateways(the_sorted_gateways, 0);
	
  int num_of_intra_neighbors = 0;
  for(int i=0; i<num_intra_gateway; i++){
    if(intra_neighbors_ts[i] > 0 && index != i) {
      num_of_intra_neighbors++;
    }
  }
  double threshold = (double)num_of_intra_neighbors * (double)pgw / 100;
  now_gateway = 0;
  for (int i = 0; (double)i < threshold; i++) {
    if (the_sorted_gateways[i] == myNodeId) {
      now_gateway = 1;
      break;
    }
  }
  printf("\nsetgateway NodeId %d the threshold %lf now_gateway %d at %lf\n", myNodeId, threshold, now_gateway, CURR_TIME);
	
  printf(" at %lf\n", CURR_TIME);
	
	
  if(now_gateway){ //let DSDV know..
    if(strcmp(baseRAgentName, "DSDV") == 0){
      ((DSDV_Agent *)baseRAgent)->my_gateway_list[index] = 1;
    }
  }
  else{ //let DSDV and AODV know..
    if(strcmp(baseRAgentName, "DSDV") == 0){
      ((DSDV_Agent *)baseRAgent)->my_gateway_list[index] = 0;
    }
    else if(strcmp(baseRAgentName, "AODV") == 0){
      for(int i=0; i< n_idrm_rt_entry; i++){
	aodv_rt_entry *rt;
	rt = ((AODV *)baseRAgent)->rtable.rt_lookup( idrm_rt_table[i].dst );
	if(rt)
	  ((AODV *)baseRAgent)->nb_delete(idrm_rt_table[i].dst);		
      }
    }
  }
	
  if(IDRM_DEBUG_0303){
    printf("0303 NodeId: %d current gts at %lf:", myNodeId, CURR_TIME);
    for(int i=0; i<num_intra_gateway; i++){
      if(intra_neighbors_ts[i] > 0) printf("%d | ", getReverseIndex(i));	
    }
    printf("\n0303 nNodeId: %d #ofGT:%d currently gateway: %d at %lf\n", myNodeId, num_intra_gateway, now_gateway, CURR_TIME);	
  }
}
//0214


void
IDRM::sendIntraRTRequest(nsaddr_t dst){
	
  if(IDRM_DEBUG)
    cout<<"sendintraRTrequest :"<<myNodeId<<endl;
	
  //send a RT request to addr
  if(identity == false) return; //0404
	
	
  Packet *p = Packet::alloc();
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type = IDRMTYPE_INTRA_ROUTE_TABLE_REQUEST;
  rh->my_gt_route = after_selection_my_routes;  //0324
	
  memset(rh->my_dst, 0, sizeof(bool) * NUM_DST);
  memcpy(rh->my_dst, intra_neighbors_after_selection,sizeof(bool) * NUM_DST);
	
  //strcpy(rh->MANET_ID, MANET_ID);
	
  ch->uid()	= beaconId++;
  ch->ptype() = PT_IDRM;
  ch->size()	= IP_HDR_LEN + sizeof(hdr_idrm);
  ch->iface() = -2;
  ch->error() = 0;
  ch->addr_type()= NS_AF_NONE;
  ch->prev_hop_  = myNodeId;			// AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_ = NETWORK_DIAMETER;
	
  if(IDRM_DEBUG)	
    printf("[IDRM SH] NodeId:%d sending Intra RTRequest to %d at %lf\n", myNodeId, dst, CURR_TIME);
	
  if(now_gateway){	
    n_out_intra_request ++;
    s_out_intra_request += ch->size();
  }
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to AODV] NodeId:%d sent Intra RTRequest to %d at %lf\n", myNodeId, dst, CURR_TIME);
		
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to DSDV] NodeId:%d sent Intra RTRequest to %d at %lf\n", myNodeId, dst, CURR_TIME);
		
    ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
  }else{
    Packet::free(p);	
  }
}

void
IDRM::sendIntraRTReply(nsaddr_t dst, int start_index){
	
  if(IDRM_DEBUG)
    cout<<"sendintrartreply :"<<myNodeId<<endl;
	
  if(identity == false) return; //0404
	
  if( n_idrm_rt_entry == 0 ) return;
	
  int i,j,c;
  Packet *p = Packet::alloc(IDRM_PACKET_SIZE);
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  //ch->size()	  = IP_HDR_LEN + sizeof(hdr_idrm) + IDRM_PACKET_SIZE; //IP hdr + IDRM hdr + payload
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	= MAX_HOP;
	
  rh->ih_type = IDRMTYPE_INTRA_ROUTE_REPLY;
  rh->my_gt_route = after_selection_my_routes;  //0324	
	
  memset(rh->my_dst, 0, sizeof(bool) * NUM_DST);
  memcpy(rh->my_dst, intra_neighbors_after_selection,sizeof(bool) * NUM_DST);
	
  c=0;
	
  //char* buf = new char[ sizeof(idrm_rt_entry) ];
  //idrm_rt_entry* idrm_payload = (idrm_rt_entry*)buf;
  idrm_rt_entry* idrm_payload = (idrm_rt_entry*)my_idrm_payload;
	
  for(i=start_index; i<n_idrm_rt_entry;i++){
		
    //if( !from_diff_domain( idrm_rt_table[i].dst ) ) continue; //only advertize the external info.
    //idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->data_)->data() + (sizeof(idrm_rt_entry)*c));
    idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->userdata())->data() + (sizeof(idrm_rt_entry)*c));
		
    //strcpy(idrm_payload->MANET_ID, idrm_rt_table[i].MANET_ID);
    idrm_payload->route_type			= 'R';
    idrm_payload->dst						= idrm_rt_table[i].dst;
    idrm_payload->hop_count			= idrm_rt_table[i].hop_count;
    // Mission Policy 
    for(j=0; j<idrm_rt_table[i].hop_count; j++){
      if(j >= MAX_HOP){break;} //0320
      idrm_payload->path[j]		= idrm_rt_table[i].path[j];
      //- idrm_payload->Domain[j]		= idrm_rt_table[i].Domain[j];
    }
		
    //memcpy(tmp, buf, sizeof(idrm_rt_entry));
    memcpy(tmp, my_idrm_payload, sizeof(idrm_rt_entry));
    c++;
		
    if( c == MAX_ROUTE_PER_PACKET )
      break;
  }
	
  //if(buf != NULL) delete []buf;
	
	
  rh->n_route = c;
	
  //for debugging...
  if(IDRM_DEBUG){
    printf("[IDRM_SH] NodeId:%d print IDRM_RT payload\n", myNodeId);
    for(i=0; i<c;i++){
      //idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i);
      idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i);
      printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
    }
  }
	
  //printf("[IDRM SH] NodeId:%d sending Intra RTReply to %d at %lf\n", myNodeId, dst, CURR_TIME);
	
  if( c == 0 ){
    Packet::free(p);
    return;
  }
	
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm) + ( sizeof(idrm_rt_entry) * c); // actual packet size
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to AODV] NodeId:%d sent Intra RTReply to %d at %lf\n", myNodeId, dst, CURR_TIME);
		
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
		
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to DSDV] NodeId:%d sent Intra RTReply to %d at %lf\n", myNodeId, dst, CURR_TIME);
		
    ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
  }  
	
  if(now_gateway){
    n_out_intra_table ++;
    s_out_intra_table += ch->size();
  }
	
  if( start_index + c < n_idrm_rt_entry ) {
    sendIntraRTReply(dst, start_index + c);
  }
  //if(!buf) delete buf;
}

void
IDRM::sendIntraRTUpdate(int index, nsaddr_t dst){
	
  if(IDRM_DEBUG)
    cout<<"sendintrartupdate :"<<myNodeId<<endl;
	
  if(identity == false) return; //0404
	
  if(IDRM_DEBUG)	
    printf("NodeId:%d, dst:%d intra lookup:%d at %lf\n", myNodeId, dst, intra_lookup(dst), CURR_TIME);
	
  if( n_idrm_rt_entry	 == 0 || 
      dst < 0 ||
      dst == myNodeId ||
      gateway_lookup(dst) < 0 ||
      intra_gateways_ts [ gateway_lookup(dst) ] < CURR_TIME //||
      //intra_lookup( dst )
      ){
    return;
  }
	
  Packet *p = Packet::alloc(IDRM_PACKET_SIZE);
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  int i,j,r;
	
  rh->ih_type = IDRMTYPE_INTRA_ROUTE_UPDATE;
  rh->my_gt_route = after_selection_my_routes;  //0324
	
  memset(rh->my_dst, 0, sizeof(bool) * NUM_DST);
  memcpy(rh->my_dst, intra_neighbors_after_selection,sizeof(bool) * NUM_DST);
	
  ch->uid()		= beaconId++;
  ch->ptype()		= PT_IDRM;
  //ch->size()	  = IP_HDR_LEN + sizeof(hdr_idrm) + IDRM_PACKET_SIZE; //IP hdr + IDRM hdr + payload
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_	= MAX_HOP;
	
  //Payload Setup: 0614
	
  //char* buf = new char[ sizeof(idrm_rt_entry) ];
  //idrm_rt_entry* idrm_payload = (idrm_rt_entry*)buf;
  idrm_rt_entry* idrm_payload = (idrm_rt_entry*)my_idrm_payload;
	
  for(i=index, r=0;i<n_idrm_rt_entry && r <MAX_ROUTE_PER_PACKET;i++) {
		
    //if ( !from_diff_domain ( idrm_rt_table[i].dst ) ) continue;  //only advertize external info
		
    if(		idrm_rt_table[i].route_type == 'N' || 
		idrm_rt_table[i].route_type == 'D' ||
		idrm_rt_table[i].route_type == 'U' ) { //not normal
			
			
      //strcpy( idrm_payload->MANET_ID, idrm_rt_table[i].MANET_ID);
      idrm_payload->route_type			= idrm_rt_table[i].route_type;
      idrm_payload->dst							= idrm_rt_table[i].dst;
      idrm_payload->hop_count				=	idrm_rt_table[i].hop_count;
      // Mission Policy
      for(j=0;j<idrm_rt_table[i].hop_count;j++) {
	if(j >= MAX_HOP){break;} //0320
	idrm_payload->path[j]			= idrm_rt_table[i].path[j];
				
      }
			
      //0718
      if(idrm_rt_table[i].route_type == 'N' || idrm_rt_table[i].route_type == 'D')
	idrm_payload->updateId		= idrm_rt_table[i].updateId;
			
			
      //idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->data_)->data() + (sizeof(idrm_rt_entry)*r));
      idrm_rt_entry* tmp = (idrm_rt_entry*) (((PacketData*)p->userdata())->data() + (sizeof(idrm_rt_entry)*r));
      //memcpy(tmp, buf, sizeof(idrm_rt_entry));
      memcpy(tmp, my_idrm_payload, sizeof(idrm_rt_entry));
			
      /* do it in sendRouteUpdate()
	 if( idrm_rt_table[i].route_type != 'D' ) {
	 idrm_rt_table[i].route_type = 'R';
	 }
	 else {
	 idrm_rt_table[i].route_type = 'T';
	 }
      */
      r++;
    }
  }
	
  //if(buf != NULL) delete []buf;
	
  //for debugging
  if(IDRM_DEBUG){
    printf("[IDRM_SH] NodeId:%d print IDRM_RT in RTUpdate r:%d payload\n", myNodeId, r);
    for(int a=0; a<r;a++){
      //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * a);
      //idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * a);
      idrm_rt_entry* tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * a);
      printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
    }
  }
	
  //rh->n_route = r+1; 0703
  rh->n_route = r;
	
  /*do it in sendRouteUpdate()
  // Delete all D route
  for(i=0;i<n_idrm_rt_entry;i++) {
  if( idrm_rt_table[i].route_type == 'T' ) {
  remove_route(i);
  i = -1;
  }
  }
  */
  dumpRT();
	
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm) + ( sizeof(idrm_rt_entry) * r); // actual packet size
	
  if( r > 0 )	 {
    if(IDRM_DEBUG)
      printf("[IDRM SH] NodeId:%d sending Intra RTUpdate to %d r:%d at %lf\n", myNodeId, dst, r, CURR_TIME);
		
    if (strcmp(baseRAgentName, "AODV") == 0){
      if(IDRM_DEBUG)	
	printf("[SH IDRM to AODV] NodeId:%d sent Intra RTUpdate to %d at %lf\n", myNodeId, dst, CURR_TIME);
			
      ((AODV*)baseRAgent)->forwardFromIDRM(p);
    }
    else if (strcmp(baseRAgentName, "DSDV") == 0){
      if(IDRM_DEBUG)	
	printf("[SH IDRM to DSDV] NodeId:%d sent Intra RTUpdate to %d at %lf\n", myNodeId, dst, CURR_TIME);
			
      ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
    }
		
    if(now_gateway){
      n_out_intra_update ++;
      s_out_intra_update += ch->size();
    }
  }
  else{
    Packet::free(p);
  }
  //if(!buf)	delete buf;
  if( r == MAX_ROUTE_PER_PACKET ) {
    sendIntraRTUpdate(i, dst);
  }
}

void
IDRM::sendIntraPacket(Packet *p, double delay){
  //send a packet through baseAgent	
  if(IDRM_DEBUG)
    cout<<"sendintrapacket :"<<myNodeId<<endl;
	
  struct hdr_ip *ih	= HDR_IP(p);
  int dst = ih->daddr();
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to AODV] NodeId:%d sent Intra Packet to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to DSDV] NodeId:%d sent Intra Packet to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
  }
  else if (strcmp(baseRAgentName, "DSR") == 0){
		
  }
  else if (strcmp(baseRAgentName, "TORA") == 0){
		
  }
  else {
    printf("IDRM_ERROR at recvData(): Unknown Base routing agent on Node %d at %lf\n", myNodeId, CURR_TIME);
    exit(1);
  }
}

void
IDRM::recvIntraRTRequest(Packet *p){
	
  if(IDRM_DEBUG)
    cout<<"recvintrartpacket :"<<myNodeId<<endl;
	
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_cmn *ch	= HDR_CMN(p);
	
  n_in_intra_request++;
  s_in_intra_request += ch->size();
	
  //printf("[IDRM SH] NodeId:%d receiving Intra RTRequest from %d at %lf\n", myNodeId, ih->saddr(), CURR_TIME);
	
  sendIntraRTReply(ih->saddr(), 0);
  Packet::free(p);
}

void
IDRM::recvIntraRTReply(Packet *p){
	
  if(IDRM_DEBUG)
    cout<<"recvintrartreply :"<<myNodeId<<endl;
	
  int i,j;
  int r;
  int new_r;
  int loop	  = 0/*,BadDomain = 0*/;
  //int exist_dst = 0;
	
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
  struct hdr_cmn *ch	= HDR_CMN(p);
	
  n_in_intra_table++;
  s_in_intra_table += ch->size();
	
  //calculating # of hops: 0706
  int intra_hops =0;
	
  if (strcmp(baseRAgentName, "AODV") == 0){
		
    aodv_rt_entry *rt;
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( ih->saddr() );
    if(rt)
      intra_hops = rt->rt_hops;	
    else{ //0323
      Packet::free(p);
      return;
    }
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
		
    RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
    rtable_ent *prte;
    prte = rt->GetEntry( ih->saddr() ); //get an entry
		
    if(prte)
      intra_hops = prte->metric;
    else{	//0323
      Packet::free(p);
      return;
    }
  }
	
  if(intra_hops >= 250) {//0323
    Packet::free(p);	
    return;
  }
	
  if(IDRM_DEBUG)
    printf("[IDRM SH] NodeId:%d receiving Intra RTReply from %d intra_hops:%d at %lf\n", 
	   myNodeId, ih->saddr(), intra_hops, CURR_TIME);
	
  idrm_rt_entry* idrm_payload;
	
  for(i=0; i<rh->n_route; i++){
		
    //idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i));
    //idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i));
    idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i));
		
    if( idrm_payload->dst == myNodeId ) continue; //added
		
    //0723: lookup eIDRM..if eIDRM has the dst, we only consider a better route..
    idrm_rt_entry*	ert = rt_lookup( idrm_payload->dst );
    if(ert){
      //if((ert->dst == idrm_payload->dst) && (( ert->hop_count - 1) <= (idrm_payload->hop_count + intra_hops)))
      if((ert->dst == idrm_payload->dst) && (( ert->hop_count - 1) < (idrm_payload->hop_count + intra_hops - 1)))
	continue;		//it is not a better route..
    }
		
    new_r = 1;
    loop = 0;
		
    for(r=0; r< idrm_payload->hop_count; r++){
      if(r >= MAX_HOP){break;} //0320
			
      if( idrm_payload->path[r] == myNodeId){
	loop = 1;
	break;	
      }
      /*- if( idrm_payload->Domain[r] == 'B' && Policy && Domain == 'A'){
	BadDomain = 1;
	break; 
	}*/
    }
		
    if(loop )
      continue;
		
    for(j=0; j<n_intra_idrm_rt_entry; j++){
      if( intra_idrm_rt_table[j].dst == idrm_payload->dst ){
				
	new_r = 0;
				
	// have existing route to dst, check hop_count is smaller or not
	if( idrm_payload->hop_count + intra_hops - 1 < intra_idrm_rt_table[j].hop_count -1 ){
					
	  //before path update, initialize
	  //memset(intra_idrm_rt_table[j].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	  for(int a=0; a<MAX_HOP; a++){
	    intra_idrm_rt_table[j].path[a] = -1;
	  }
	  // Mission Policy
	  intra_idrm_rt_table[j].route_type		= 'U';
	  intra_idrm_rt_table[j].path[0]				= ih->saddr();
					
					
	  for(r=0; r<idrm_payload->hop_count; r++){
	    if(r >= MAX_HOP-1){break;} //0320
	    intra_idrm_rt_table[j].path[r+1]		=	idrm_payload->path[r];
						
	  }
					
	  intra_idrm_rt_table[j].next_hop				=	ih->saddr();
	  intra_idrm_rt_table[j].hop_count			=	idrm_payload->hop_count + intra_hops;// 0706 1;
	  intra_idrm_rt_table[j].recv_t					= CURR_TIME;
	  intra_idrm_rt_table[j].expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;
	}
	else if((idrm_payload->hop_count + intra_hops - 1 == intra_idrm_rt_table[j].hop_count -1) &&
		(ih->saddr()	== intra_idrm_rt_table[j].next_hop)){		//for the same route, update the expire timer
	  //(idrm_payload->next_hop	== intra_idrm_rt_table[j].next_hop)){		//for the same route, update the expire timer
					
	  intra_idrm_rt_table[j].route_type		= 'U';
	  intra_idrm_rt_table[j].recv_t					= CURR_TIME;
	  intra_idrm_rt_table[j].expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;
	}
      }
    }			
		
    // Find new route, add to route table
    if( new_r == 1 ){ 
      intra_idrm_rt_table[n_intra_idrm_rt_entry].route_type				= 'N';
      intra_idrm_rt_table[n_intra_idrm_rt_entry].dst							= idrm_payload->dst;
			
      //memset(intra_idrm_rt_table[n_intra_idrm_rt_entry].path, -1,sizeof(nsaddr_t)*MAX_HOP);
      for(int a=0; a<MAX_HOP; a++){
	intra_idrm_rt_table[n_intra_idrm_rt_entry].path[a] = -1;
      }
      // Mission Policy
      intra_idrm_rt_table[n_intra_idrm_rt_entry].path[0]					= ih->saddr();
			
			
      for(r=0; r< idrm_payload->hop_count; r++){
	if(r >= MAX_HOP-1){break;} //0320
	intra_idrm_rt_table[n_intra_idrm_rt_entry].path[r+1]			=	idrm_payload->path[r];	
				
      }
			
      intra_idrm_rt_table[n_intra_idrm_rt_entry].next_hop					= ih->saddr();
      intra_idrm_rt_table[n_intra_idrm_rt_entry].hop_count				= idrm_payload->hop_count+ intra_hops; //0706 1;
      intra_idrm_rt_table[n_intra_idrm_rt_entry].recv_t						= CURR_TIME;
      intra_idrm_rt_table[n_intra_idrm_rt_entry].expire_t					=	CURR_TIME + IDRM_ROUTE_EXPIRY;
			
      //0718
      intra_idrm_rt_table[n_intra_idrm_rt_entry].updateId					= updateId;
      updateId++;
			
      //changed 0817
      if(IDRM_DEBUG)
	printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvIntraRTReply iIDRM)\n",
	       myNodeId, intra_idrm_rt_table[n_intra_idrm_rt_entry].route_type, intra_idrm_rt_table[n_intra_idrm_rt_entry].updateId, 
	       intra_idrm_rt_table[n_intra_idrm_rt_entry].dst, CURR_TIME);
			
      n_intra_idrm_rt_entry++;
    }
		
  }
	
  //for debugging...
  if(IDRM_DEBUG)
    printf("[IDRM_SH] NodeId:%d print IDRM_RT payload\n", myNodeId);
	
  idrm_rt_entry* tmp;
  for(i=0; i<rh->n_route;i++){
    //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i);
    //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i);
    tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i);
    if(IDRM_DEBUG)
      printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
  }
	
  Packet::free(p);
	
  /*
  //Tracking the current request
  if(ih->saddr() == trackRequestNode){
  //free
  trackRequest = false;	
  }
  */
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG After receiving Intra RT PACKET, node %d RT is at %lf:\n", myNodeId, CURR_TIME);
  dumpIntraRT();
	
  maintainIntraRT();
}

void
IDRM::recvIntraRTUpdate(Packet *p){
	
  if(IDRM_DEBUG)
    cout<<"recvintrartupdate :"<<myNodeId<<endl;
	
  int i,j,r;
  int new_r, loop/*, BadDomain*/;
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
  struct hdr_cmn *ch	= HDR_CMN(p);
	
  n_in_intra_update++;
  s_in_intra_update += ch->size();
	
  nsaddr_t pkt_src = ih->saddr();
	
  int intra_hops =0; //calculating # of hops: 0706
	
  if (strcmp(baseRAgentName, "AODV") == 0){
		
    aodv_rt_entry *rt;
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( ih->saddr() );
    if(rt)
      intra_hops = rt->rt_hops;	
    else{	//0323
      Packet::free(p);
      return;
    }
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
		
    RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
    rtable_ent *prte;
    prte = rt->GetEntry( ih->saddr() ); //get an entry
		
    if(prte)
      intra_hops = prte->metric;
    else{	//0323
      Packet::free(p);
      return;
    }
  }
	
  if(IDRM_DEBUG)
    printf("[IDRM SH] NodeId:%d receiving Intra Update from %d intra_hops:%d at %lf\n", 
	   myNodeId, ih->saddr(), intra_hops, CURR_TIME);
	
	
  dumpPacket(p);
  dumpIntraRT();
	
  if(IDRM_DEBUG)
    printf("intra_hops:%d\n", intra_hops);
	
  if(intra_hops >= 250){//0323
    Packet::free(p);	
    return;
  }
	
  //IDRM_SH: 0614 look at payload...
  idrm_rt_entry* idrm_payload;
	
  for(i=0;i<rh->n_route;i++) {
		
    //idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i));
    //idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i));
    idrm_payload = ((idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i));
		
    if( idrm_payload->dst == myNodeId ) continue; //added
		
    //0723: lookup eIDRM..if eIDRM has the dst, we only consider a better route..
    idrm_rt_entry*	ert = rt_lookup( idrm_payload->dst );
    if(ert){
      //if((ert->dst == idrm_payload->dst) && (( ert->hop_count - 1) <= (idrm_payload->hop_count + intra_hops)))
      if((ert->dst == idrm_payload->dst) && (( ert->hop_count - 1) < (idrm_payload->hop_count + intra_hops - 1)))
	continue;		//it is not a better route..
    }
		
    // Others Remove route
    if( idrm_payload->route_type == 'D'){
			
      for(j=0;j<n_intra_idrm_rt_entry;j++)  {
	if( intra_idrm_rt_table[j].next_hop == pkt_src && intra_idrm_rt_table[j].dst == idrm_payload->dst){
	  intra_idrm_rt_table[j].route_type = 'D';
					
	  //0718: 'D' event propagated!
	  intra_idrm_rt_table[j].updateId	 = idrm_payload->updateId;
					
	  /*
	    printf("[IDRM_SH_Update Rec] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvRouteUpdate iIDRM)\n",
	    myNodeId, intra_idrm_rt_table[j].route_type, intra_idrm_rt_table[j].updateId, 
	    intra_idrm_rt_table[j].dst, CURR_TIME);
	  */			
	  //do it in maintainIntraRT()				
	  //if (strcmp(baseRAgentName, "AODV") == 0)
	  //	((AODV *)baseRAgent)->rtable.rt_delete( idrm_rt_table[j].dst ); //0623
	}
      }
    }
		
    // Others Add route
    if( idrm_payload->route_type == 'N' || idrm_payload->route_type == 'U'){
			
      new_r = 1;
      loop = 0;			// check loop
			
      for(r=0; r<idrm_payload->hop_count; r++){
	if(r >= MAX_HOP){break;} //0320
	if( idrm_payload->path[r] == myNodeId){
	  loop = 1;
	  break;
	}
	/*- if( idrm_payload->Domain[r] == 'B' && Policy && Domain == 'A'){
	  BadDomain = 1;
	  break;
	  }*/
      }
			
      if (loop /*|| BadDomain*/) 
	continue;
			
      for(j=0;j<n_intra_idrm_rt_entry;j++){
	if( intra_idrm_rt_table[j].dst == idrm_payload->dst){
					
	  new_r = 0;
					
	  // have existing route to dst, check hop_count is smaller or not
	  if( idrm_payload->hop_count + intra_hops - 1 < intra_idrm_rt_table[j].hop_count -1 ){
						
	    //before path update, initialize
	    //memset(intra_idrm_rt_table[j].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	    for(int a=0; a<MAX_HOP; a++){
	      intra_idrm_rt_table[j].path[a] = -1;
	    }
	    // Mission Policy
	    intra_idrm_rt_table[j].path[0]	 = ih->saddr();
						
	    for(r=0; r<idrm_payload->hop_count; r++){
	      if(r >= MAX_HOP-1){break;} //0320
	      intra_idrm_rt_table[j].path[r+1]	= idrm_payload->path[r];
							
	    }
						
	    intra_idrm_rt_table[j].next_hop	 = ih->saddr();
	    intra_idrm_rt_table[j].hop_count = idrm_payload->hop_count + intra_hops;//0706 1;
	    intra_idrm_rt_table[j].route_type = 'U';
	    intra_idrm_rt_table[j].recv_t	  = CURR_TIME;
	    intra_idrm_rt_table[j].expire_t		= CURR_TIME + IDRM_ROUTE_EXPIRY;
						
	  }
	  else if((idrm_payload->hop_count + intra_hops - 1 == intra_idrm_rt_table[j].hop_count -1) &&
		  (ih->saddr()	== intra_idrm_rt_table[j].next_hop)){		//for the same route, update the expire timer
	    //(idrm_payload->next_hop	== intra_idrm_rt_table[j].next_hop)){		//for the same route, update the expire timer
						
	    intra_idrm_rt_table[j].route_type		= 'U';
	    intra_idrm_rt_table[j].recv_t					= CURR_TIME;
	    intra_idrm_rt_table[j].expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;
	  } 
	}
      }
			
      if( new_r ) {
				
	intra_idrm_rt_table[n_intra_idrm_rt_entry].route_type	= 'N';
	intra_idrm_rt_table[n_intra_idrm_rt_entry].dst			= idrm_payload->dst;
				
	//memset(intra_idrm_rt_table[n_intra_idrm_rt_entry].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	for(int a=0; a<MAX_HOP; a++){
	  intra_idrm_rt_table[n_intra_idrm_rt_entry].path[a] = -1;	
	}
				
	intra_idrm_rt_table[n_intra_idrm_rt_entry].path[0]		= ih->saddr();
				
				
	for(r=0; r<idrm_payload->hop_count; r++){
	  if(r >= MAX_HOP-1){break;} //0320
	  intra_idrm_rt_table[n_intra_idrm_rt_entry].path[r+1]   = idrm_payload->path[r];
					
	}
				
	intra_idrm_rt_table[n_intra_idrm_rt_entry].next_hop	 = ih->saddr();
	intra_idrm_rt_table[n_intra_idrm_rt_entry].hop_count = idrm_payload->hop_count + intra_hops; // 0706 + 1;
	intra_idrm_rt_table[n_intra_idrm_rt_entry].recv_t	 = CURR_TIME;
	intra_idrm_rt_table[n_intra_idrm_rt_entry].expire_t	 = CURR_TIME + IDRM_ROUTE_EXPIRY;
				
	//0718: 'N' event propagated! // we only consider 'N'
	if(idrm_payload->route_type == 'N'){
	  intra_idrm_rt_table[n_intra_idrm_rt_entry].updateId	 = idrm_payload->updateId;
					
	  /*
	    printf("[IDRM_SH_Update Rec] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvRouteUpdate iIDRM)\n",
	    myNodeId, intra_idrm_rt_table[n_intra_idrm_rt_entry].route_type, intra_idrm_rt_table[n_intra_idrm_rt_entry].updateId,
	    intra_idrm_rt_table[n_intra_idrm_rt_entry].dst,  CURR_TIME);
	  */
	}
	else{
	  intra_idrm_rt_table[n_intra_idrm_rt_entry].updateId	 = updateId;
	  updateId++;
					
	  //changed 0817
	  /*
	    printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvRouteUpdate iIDRM)\n",
	    myNodeId, intra_idrm_rt_table[n_intra_idrm_rt_entry].route_type, intra_idrm_rt_table[n_intra_idrm_rt_entry].updateId,
	    intra_idrm_rt_table[n_intra_idrm_rt_entry].dst,  CURR_TIME);
	  */
	}
				
	n_intra_idrm_rt_entry++;
      }
    }
  }
	
	
  if(IDRM_DEBUG)
    printf("[IDRM_SH] NodeId:%d print IDRM_RT payload\n", myNodeId);
	
  idrm_rt_entry* tmp;
  for(i=0; i<rh->n_route;i++){
    //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data_ + sizeof(idrm_rt_entry) * i);
    //tmp = (idrm_rt_entry*)(((PacketData*)p->data_)->data() + sizeof(idrm_rt_entry) * i);
    tmp = (idrm_rt_entry*)(((PacketData*)p->userdata())->data() + sizeof(idrm_rt_entry) * i);
    if(IDRM_DEBUG)
      printf("	dst:%d, route_type:%c, hop_count:%d\n", tmp->dst, tmp->route_type, tmp->hop_count);
  }
	
	
  Packet::free(p);
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG AFTER RECV Intra RT Update, %d->%d time %.4lf:\n", pkt_src, myNodeId, CURR_TIME);
  dumpIntraRT();
	
  maintainIntraRT();
}

void
IDRM::maintainIntraRT(){
  /*
    todo: maintain intra_rt
    access eIDRM rt
    access base	rt
    sendRouteUpdate() : eIDRM update
  */
	
  if(IDRM_DEBUG)
    cout<<"maintainintrart :"<<myNodeId<<endl;
	
  if(IDRM_DEBUG)
    printf("NodeId:%d maintainIntraRT start at %lf\n", myNodeId, CURR_TIME);
  dumpIntraRT();
	
  if(IDRM_DEBUG)
    printf("NodeId:%d iIDRM to eIDRM at %lf\n", myNodeId, CURR_TIME);
  iIDRMtoeIDRM();
	
  if(IDRM_DEBUG)
    printf("NodeId:%d iIDRM to base at %lf\n", myNodeId, CURR_TIME);
	
  iIDRMtoBase();
	
  int change;
  //int deleted = 0;
  int i,j,k;
	
  // Delete Expire route
  // We will send the update of deleted route later in this function
  for(i=0;i<n_intra_idrm_rt_entry;i++)  {
    if( intra_idrm_rt_table[i].expire_t < CURR_TIME) {
      intra_idrm_rt_table[i].route_type = 'D';
      //total_del_intra_routes++;
    }
    //cs218_idrm2 delete inactive gateway's external routes
    if (!now_gateway) {
      if (intra_idrm_rt_table[i].dst / num_domain_members != 
	  myNodeId / num_domain_members) //if dst is not in this domain
	intra_idrm_rt_table[i].route_type = 'D';
    }
  }
	
  //recursive check deleted route
  change = 1;
  while( change ) {
    change = 0;
    for(i=0;i<n_intra_idrm_rt_entry;i++)
      if( intra_idrm_rt_table[i].route_type == 'D' ) {
	for(j=0;j<n_intra_idrm_rt_entry;j++) 
	  if( intra_idrm_rt_table[j].route_type != 'D' ) {
	    for(k=0;k<intra_idrm_rt_table[j].hop_count;k++) {
	      if(k >= MAX_HOP){break;} //0320
	      if( intra_idrm_rt_table[j].path[k] == intra_idrm_rt_table[i].dst) {
		intra_idrm_rt_table[j].route_type = 'D';
		change	= 1;
	      }
	    }
	  }
      }
  }
	
	
  for(i=0; i<n_intra_idrm_rt_entry; i++){
    if( intra_idrm_rt_table[i].route_type != 'D' ) {
      intra_idrm_rt_table[i].route_type = 'R';
    }
    else {
      if(IDRM_DEBUG)
	printf("ready to delete %d\n", intra_idrm_rt_table[i].dst);
      remove_intra_route(i);
    }
  }
	
  if(IDRM_DEBUG)
    printf("NodeId:%d maintainIntraRT end at %lf\n", myNodeId, CURR_TIME);
  dumpIntraRT();
}

void
IDRM::iIDRMtoeIDRM(){
  /*
    todo: upload iIDRM entries to eIDRM
    do eIDRM local update
    sendRouteUpdate() : for only eIDRM update
  */ 
	
  if(IDRM_DEBUG)
    cout<<"iidrmtoeidrm :"<<myNodeId<<endl;
	
  if(IDRM_DEBUG)
    printf("NodeId:%d iIDRM to eIDRM start at %lf\n", myNodeId, CURR_TIME);
  dumpRT();
	
  int new_r, loop;
  int i,j,r;
  intra_updated_counter=0;
	
  for(i=0; i<n_intra_idrm_rt_entry; i++){
		
    if(intra_idrm_rt_table[i].dst == myNodeId) continue; //in case
		
    if(intra_idrm_rt_table[i].route_type == 'D'){
      for(j=0;j<n_idrm_rt_entry;j++)	{
	if( idrm_rt_table[j].next_hop == intra_idrm_rt_table[i].next_hop && 
	    idrm_rt_table[j].dst == intra_idrm_rt_table[i].dst){
	  idrm_rt_table[j].route_type = 'D';
					
	  //remember what entries are upated by iIDRM
	  intra_updated_entry[intra_updated_counter] = idrm_rt_table[j].dst;
	  intra_updated_counter++;
					
	  //0718: 'D' event propagate
	  idrm_rt_table[j].updateId	 = intra_idrm_rt_table[i].updateId; //internal processing
					
	  //will be handled in iIDRMtoBase				
	  //if (strcmp(baseRAgentName, "AODV") == 0)
	  //	((AODV *)baseRAgent)->rtable.rt_delete( idrm_rt_table[j].dst ); //0623
	}
      }
    }
		
    if(intra_idrm_rt_table[i].route_type == 'N' || intra_idrm_rt_table[i].route_type == 'U'){
      new_r = 1;
      loop	= 0;
			
      for(r=0; r<intra_idrm_rt_table[i].hop_count; r++){
	if(r >= MAX_HOP){break;} //0320
	if( intra_idrm_rt_table[i].path[r] == myNodeId){
	  loop = 1;
	  break;
	}
				
      }
			
      if (loop ) continue;
			
      for(j=0;j<n_idrm_rt_entry;j++){
	if( idrm_rt_table[j].dst == intra_idrm_rt_table[i].dst){
					
	  new_r = 0;
	  // Mission Policy
	  // have existing route to dst, check hop_count is smaller or not
	  if( intra_idrm_rt_table[i].hop_count < idrm_rt_table[j].hop_count){ //0702
						
	    //before path update, initialize
	    //memset(idrm_rt_table[j].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	    for(int a=0; a<MAX_HOP; a++){
	      idrm_rt_table[j].path[a] = -1;
	    }
						
	    idrm_rt_table[j].path[0]   = intra_idrm_rt_table[i].path[0];
						
	    for(r=0; r<intra_idrm_rt_table[i].hop_count; r++){
	      if(r >= MAX_HOP){break;} //0320
	      idrm_rt_table[j].path[r]	= intra_idrm_rt_table[i].path[r];
							
	    }
						
	    idrm_rt_table[j].next_hop	= intra_idrm_rt_table[i].next_hop;
	    idrm_rt_table[j].hop_count	= intra_idrm_rt_table[i].hop_count;
	    idrm_rt_table[j].route_type = 'U';
	    idrm_rt_table[j].recv_t		= intra_idrm_rt_table[i].recv_t;
	    idrm_rt_table[j].expire_t	= intra_idrm_rt_table[i].expire_t;
						
						
	    intra_updated_entry[intra_updated_counter] = idrm_rt_table[j].dst;
	    intra_updated_counter++;
						
	  }//changed 0625
	  else if((intra_idrm_rt_table[i].dst == idrm_rt_table[j].dst) && 
		  (intra_idrm_rt_table[i].next_hop == idrm_rt_table[j].next_hop)){
	    idrm_rt_table[j].route_type = 'U'; //0702
	    idrm_rt_table[j].recv_t	   = intra_idrm_rt_table[i].recv_t;
	    idrm_rt_table[j].expire_t  = intra_idrm_rt_table[i].expire_t;
						
	    intra_updated_entry[intra_updated_counter] = idrm_rt_table[j].dst;
	    intra_updated_counter++;
	  }
	}
      }
			
      if( new_r ) {
				
	idrm_rt_table[n_idrm_rt_entry].route_type	= 'N';
	idrm_rt_table[n_idrm_rt_entry].dst			= intra_idrm_rt_table[i].dst;
				
	//memset(idrm_rt_table[n_idrm_rt_entry].path, -1, sizeof(nsaddr_t)*MAX_HOP);
	for(int a=0; a<MAX_HOP; a++){
	  idrm_rt_table[n_idrm_rt_entry].path[a] = -1;
	}
				
				
	idrm_rt_table[n_idrm_rt_entry].path[0]		= intra_idrm_rt_table[i].path[0];
				
				
	for(r=0; r<intra_idrm_rt_table[i].hop_count; r++){
	  if(r >= MAX_HOP){break;} //0320
	  idrm_rt_table[n_idrm_rt_entry].path[r]	 = intra_idrm_rt_table[i].path[r];
					
	}
				
	idrm_rt_table[n_idrm_rt_entry].next_hop	 = intra_idrm_rt_table[i].next_hop;
	idrm_rt_table[n_idrm_rt_entry].hop_count = intra_idrm_rt_table[i].hop_count;
	idrm_rt_table[n_idrm_rt_entry].recv_t	 = intra_idrm_rt_table[i].recv_t;
	idrm_rt_table[n_idrm_rt_entry].expire_t	 = intra_idrm_rt_table[i].expire_t;
				
				
	if(intra_idrm_rt_table[i].route_type == 'N'){
	  //0718: 'N' event propagate
	  idrm_rt_table[n_idrm_rt_entry].updateId	 = intra_idrm_rt_table[i].updateId; //internal processing
					
	  /*
	    printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (recvIntraRTReply iIDRM)\n",
	    myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	    idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
	  */
					
	}
				
	intra_updated_entry[intra_updated_counter] = idrm_rt_table[n_idrm_rt_entry].dst;
	intra_updated_counter++;
				
	n_idrm_rt_entry++;
      }
    }
  }
	
  /*
  //do local RT_maintain...: do local update! 
  //only send out updated info to other domains(broadcast)...
  //do not send Intra-update
  int change;
  int deleted = 0;
	 
	 
  // Delete Expire route
  // We will send the update of deleted route later in this function
  for(i=0;i<n_idrm_rt_entry;i++)	 {
  if( idrm_rt_table[i].expire_t < CURR_TIME) {
  idrm_rt_table[i].route_type = 'D';
  total_del_routes++;
  }
  }
	 
  //recursive check deleted route
  change = 1;
  while( change ) {
  change = 0;
  for(i=0;i<n_idrm_rt_entry;i++)
  if( idrm_rt_table[i].route_type == 'D' ) {
  for(j=0;j<n_idrm_rt_entry;j++) 
  if( idrm_rt_table[j].route_type != 'D' ) {
  for(k=0;k<idrm_rt_table[j].hop_count;k++) {
  if( idrm_rt_table[j].path[k] == idrm_rt_table[i].dst) {
  idrm_rt_table[j].route_type = 'D';
  change	 = 1;
  }
  }
  }
  }
  }
  */ 
  //sendRouteUpdate(IIDRM);
	
  //printf("p0 id: %d intra_counter:%d\n", myNodeId, intra_updated_counter);
	
  sendRouteUpdatebyIIDRM();
	
  //memset(intra_updated_entry, -1, sizeof(nsaddr_t) * MAX_ROUTE);	//reset the entry list 0902
  for(int a=0; a< MAX_ROUTE; a++){
    intra_updated_entry[a] = -1;
  }
	
	
  // Should send out route update for fast convergence
	
  if(IDRM_DEBUG)
    printf("NodeId:%d iIDRM to eIDRM end at %lf\n", myNodeId, CURR_TIME);
  dumpRT();
	
}

void
IDRM::iIDRMtoBase(){
  //this function is only for AODV
	
  if(IDRM_DEBUG)
    cout<<"iidrmtobase :"<<myNodeId<<endl;
	
  if(IDRM_DEBUG)
    printf("NodeId:%d iIDRM to Base start at %lf\n", myNodeId, CURR_TIME);
  dumpBaseRT();
	
	
  if (strcmp(baseRAgentName, "AODV") != 0) return;
	
  int i;
  aodv_rt_entry *rt0;
  aodv_rt_entry *rt;
	
  for(i=0; i<n_intra_idrm_rt_entry; i++){
		
    if(intra_idrm_rt_table[i].dst == myNodeId) continue; //in case
		
    rt0 = ((AODV *)baseRAgent)->rtable.rt_lookup( intra_idrm_rt_table[i].next_hop );
    if(!rt0) return;
		
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( intra_idrm_rt_table[i].dst );
		
		
    if(intra_idrm_rt_table[i].route_type == 'D'){
      if(!rt) return;
      if(rt0->rt_nexthop == rt->rt_nexthop && intra_idrm_rt_table[i].hop_count == rt->rt_hops) //check it is from me
	((AODV *)baseRAgent)->rtable.rt_delete( intra_idrm_rt_table[i].dst );
      return;
    }
		
    if(!rt){
      rt = ((AODV *)baseRAgent)->rtable.rt_add(	intra_idrm_rt_table[i].dst );
      rt->rt_seqno			= 1;//intra_idrm_rt_table[i].seqno + 1000;
      rt->rt_hops				= intra_idrm_rt_table[i].hop_count;
      rt->rt_flags			= RTF_UP;
      rt->rt_nexthop		= rt0->rt_nexthop;
      rt->rt_interface  = 0; //through iIDRM!
      rt->rt_expire			= intra_idrm_rt_table[i].expire_t;
    }
    else if( rt->rt_flags != RTF_UP){
      //rt->rt_seqno			+= 1;//idrm_rt_table[i].seqno + 1000; // to avoid conflict in case...
      rt->rt_hops				= intra_idrm_rt_table[i].hop_count;
      rt->rt_flags			= RTF_UP;
      rt->rt_nexthop		= rt0->rt_nexthop;
      rt->rt_interface  = 0; //through iIDRM!
      rt->rt_expire			= intra_idrm_rt_table[i].expire_t;
    }
    else if( intra_idrm_rt_table[i].hop_count < rt->rt_hops	 ){ //better route
      //rt->rt_seqno			= intra_idrm_rt_table[i].seqno + 1000; // to avoid conflict in case...
      rt->rt_hops				= intra_idrm_rt_table[i].hop_count;
      rt->rt_flags			= RTF_UP;
      rt->rt_nexthop		= rt0->rt_nexthop;
      rt->rt_interface  = 0; //through iIDRM!
      rt->rt_expire			= intra_idrm_rt_table[i].expire_t;
    }
    else{//found: update timestamp!
      if(rt0->rt_nexthop == rt->rt_nexthop && intra_idrm_rt_table[i].hop_count == rt->rt_hops)
	rt->rt_expire		= intra_idrm_rt_table[i].expire_t;
    }
  }
	
  if(IDRM_DEBUG)
    printf("NodeId:%d iIDRM to Base end at %lf\n", myNodeId, CURR_TIME);
  dumpBaseRT();
}

void
IDRM::update_intra_neighbors(nsaddr_t neighbor_id){
  //update MANET_ID
  cout<<"updateintraneighbors :"<<myNodeId<<endl;
  intra_gateways_ts[neighbor_id] = CURR_TIME + IDRM_INTRA_GATEWAY_EXPIRY;
  int counter=1;
	
  for(int i=0; i<MAX_INTRA_GATEWAYS; i++){
		
    if( intra_gateways[i] > CURR_TIME ) intra_gateways[i] = -1; //expired!
		
    if( i == myNodeId ){ //handle myself
      MANET_ID[counter] = i;
      counter++;
      continue;
    }
		
    if( intra_gateways[i] > 0 ){ //valid intra-gateway
      MANET_ID[counter] = i;
      counter++;
    }
  }
  //MANET_ID is updated!!
}

bool
IDRM::detect_partition(){
  //not being used now
  bool changed = false;
	
  for( int i=0; i<num_intra_gateway; i++){
		
    if( intra_gateways[i] > CURR_TIME){
      intra_gateways[i] = -1; //expired
      changed = true; 
    }
  }
	
  if(changed){
    //generate a new MANET ID		
		
    return true; //domain is partitioned.
  }
	
  return false; //not partitioned
}

bool
IDRM::manetIdCompare(int* in1, int* in2){
  //not being used now
  for(int i=1; i<32; i++)
    if(in1[i] != in2[i]) return false; //different id
	
  return true; //same id
}
// iIDRM
// ======================================================================

// ======================================================================
// Base - IDRM

// =================================================
// Base routing agent routing table READING routines

void
IDRM::aodvRTReadTrackChanges(void){
  aodv_rt_entry *rt;
	
  if(IDRM_DEBUG)
    printf("NodeId:%d print BaseRT at RT exchange start(eIDRM -> Base, Base -> eIDRM) at %lf\n", myNodeId, CURR_TIME);
  dumpBaseRT();
  dumpRT();
	
  /*
    WRITE the CODE here for modifications based on changes detected by IDRM agent
    need to keep track od previous state of routing table to detect changes
    IDRM_SH: table exchange
    RT exchange: IDRM <--> BaseAgent(AODV)
    1) IDRM -> BaseAgent : lookup: found->update timestamp, no:Add!!
  */ 
	
  for(int i=0; i<n_idrm_rt_entry; i++){
		
    //if ( idrm_rt_table[i].expire_t < CURR_TIME ) continue; 
    if( idrm_rt_table[i].route_type == 'D' || idrm_rt_table[i].route_type == 'T' ) continue;
		
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( idrm_rt_table[i].dst );		
		
    if( rt == 0 ){ //not found
			
      rt = ((AODV *)baseRAgent)->rtable.rt_add(	idrm_rt_table[i].dst );
      rt->rt_seqno			= 1;//idrm_rt_table[i].seqno + 1000; // to avoid conflict in case...
      rt->rt_hops				= idrm_rt_table[i].hop_count;
      rt->rt_flags			= RTF_UP;
      rt->rt_nexthop		= idrm_rt_table[i].next_hop;
      rt->rt_interface  = 1; //through IDRM!
      rt->rt_expire			= idrm_rt_table[i].expire_t;
    }
    else if( rt->rt_flags != RTF_UP){
      //rt->rt_seqno			+= 1;//idrm_rt_table[i].seqno + 1000; // to avoid conflict in case...
      rt->rt_hops				= idrm_rt_table[i].hop_count;
      rt->rt_flags			= RTF_UP;
      rt->rt_nexthop		= idrm_rt_table[i].next_hop;
      rt->rt_interface  = 1; //through IDRM!
      rt->rt_expire			= idrm_rt_table[i].expire_t;
    }
    else if( idrm_rt_table[i].hop_count < rt->rt_hops  ){ //better route
			
      //rt->rt_seqno			+= 1;//idrm_rt_table[i].seqno + 1000; // to avoid conflict in case...
      rt->rt_hops				= idrm_rt_table[i].hop_count;
      rt->rt_flags			= RTF_UP;
      rt->rt_nexthop		= idrm_rt_table[i].next_hop;
      rt->rt_interface  = 1; //through IDRM!
      rt->rt_expire			= idrm_rt_table[i].expire_t;
    }
    else{//found: update timestamp!
      if( rt->rt_interface == 1 )
	rt->rt_expire = idrm_rt_table[i].expire_t;
    }
  }
	
  //2) BaseAgent -> IDRM : lookup: found->update timestamp, no:Add!!
  idrm_rt_entry* idrm_rt = NULL;
	
  for(rt = ((AODV *)baseRAgent)->rtable.head(); rt; rt = rt->rt_link.le_next) {
		
    idrm_rt = rt_lookup( rt->rt_dst );
		
    if ((CURR_TIME < rt->rt_expire ) && (rt->rt_flags == RTF_UP)) {
			
      if( rt->rt_dst == myNodeId ) continue;								
      if( idrm_rt == NULL ){//not found --> add (they are to be used for RT exchange)
				
	idrm_rt_table[n_idrm_rt_entry].dst				= rt->rt_dst;
	idrm_rt_table[n_idrm_rt_entry].hop_count	= rt->rt_hops;
	idrm_rt_table[n_idrm_rt_entry].next_hop		= rt->rt_nexthop;
	idrm_rt_table[n_idrm_rt_entry].expire_t		= rt->rt_expire + IDRM_ROUTE_EXPIRY -10 ;//IDRM_ROUTE_EXPIRY + CURR_TIME; //rt->rt_expire;//CURR_TIME + IDRM_ROUTE_EXPIRY;//
	idrm_rt_table[n_idrm_rt_entry].seqno			= rt->rt_seqno + 1000; //to avoid conflict in case...
	idrm_rt_table[n_idrm_rt_entry].route_type = 'N';//added 0614
				
	//memset(idrm_rt_table[n_idrm_rt_entry].path, -1, sizeof(nsaddr_t) *MAX_HOP);
	for(int a=0; a<MAX_HOP; a++){
	  idrm_rt_table[n_idrm_rt_entry].path[a] = -1;
	}
				
				
	idrm_rt_table[n_idrm_rt_entry].path[0]		= rt->rt_dst;
				
	//0718: 'N' event happen
	idrm_rt_table[n_idrm_rt_entry].updateId	 = updateId;
	updateId++;
				
	/*
	  printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (from BaseAgent)\n",
	  myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	  idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
	*/
				
	n_idrm_rt_entry++;	
      }
      else if( rt->rt_hops < idrm_rt->hop_count ){ //better route
	idrm_rt->dst				= rt->rt_dst;
	idrm_rt->hop_count	= rt->rt_hops;
	idrm_rt->next_hop	= rt->rt_nexthop;
	idrm_rt->expire_t	= rt->rt_expire + IDRM_ROUTE_EXPIRY -10 ;//CURR_TIME + IDRM_ROUTE_EXPIRY;//rt->rt_expire;		//CURR_TIME + IDRM_ROUTE_EXPIRY;//
	idrm_rt->route_type = 'U';//added 0614
				
	memset(idrm_rt->path, -1, sizeof(nsaddr_t) *MAX_HOP);
	idrm_rt->path[0]		= rt->rt_dst; //0
      }
      else{//found
	if( !intra_rt_lookup(rt->rt_dst) && rt->rt_interface == 0){ //it came from baseAgent
					
	  idrm_rt->expire_t	= rt->rt_expire + IDRM_ROUTE_EXPIRY -10 ;//CURR_TIME + IDRM_ROUTE_EXPIRY;//rt->rt_expire; //update!		
	  idrm_rt->route_type =	'U';
	}
      }
    }
  }
	
  if(IDRM_DEBUG)	
    printf("NodeId:%d print BaseRT at RT exchange end(eIDRM -> Base, Base -> eIDRM) at %lf\n", myNodeId, CURR_TIME);
  dumpRT();
  dumpBaseRT();
	
	
  //IDRM_SH
}

void
IDRM::dsdvRTReadTrackChanges(void){
  //IDRM_SH
	
  if(IDRM_DEBUG)
    cout<<"dsdvrtreadtrackchanges :"<<myNodeId<<endl;
	
  RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
  rtable_ent *prte;
	
  if(IDRM_DEBUG)	
    printf("NodeId:%d print BaseRT at RT exchange start(eIDRM <- Base) at %lf\n", myNodeId, CURR_TIME);
  dumpRT();
  dumpBaseRT();
  //BaseAgent -> eIDRM : lookup: found->update timestamp, no:Add!!
	
  rt = ((DSDV_Agent*)baseRAgent)->table_;
  idrm_rt_entry* idrm_rt;
	
  for(rt->InitLoop(); (prte = rt->NextLoop()); ){
		
    if(prte->dst == myNodeId || prte->metric == 250 ) continue;
		
    idrm_rt = rt_lookup( prte->dst );	
		
    if( idrm_rt == NULL){//not found --> add (they are to be used for RT exchange)
			
      if( prte->metric == 250 ) continue;
			
      idrm_rt_table[n_idrm_rt_entry].dst				= prte->dst;
      idrm_rt_table[n_idrm_rt_entry].hop_count	= prte->metric;
      idrm_rt_table[n_idrm_rt_entry].next_hop		= prte->hop;
      idrm_rt_table[n_idrm_rt_entry].expire_t		= CURR_TIME + IDRM_ROUTE_EXPIRY;//prte->changed_at + IDRM_ROUTE_EXPIRY; //CURR_TIME + IDRM_ROUTE_EXPIRY;//
      //idrm_rt_table[n_idrm_rt_entry].seqno			= prte->seqnum + 1000; //to avoid conflict in case...
      //if(idrm_rt_table[n_idrm_rt_entry].seqno % 2 != 0) idrm_rt_table[n_idrm_rt_entry].seqno +=1;
			
      idrm_rt_table[n_idrm_rt_entry].route_type = 'N';//added 0614
			
      //memset(idrm_rt_table[n_idrm_rt_entry].path, -1, sizeof(nsaddr_t) *MAX_HOP);
      for(int a=0; a<MAX_HOP; a++){
	idrm_rt_table[n_idrm_rt_entry].path[a] = -1;
      }
			
      idrm_rt_table[n_idrm_rt_entry].path[0]		= prte->dst;//prte->hop;
			
      //0718: 'N' event happen
      idrm_rt_table[n_idrm_rt_entry].updateId	 = updateId;
      updateId++;
			
      if(IDRM_DEBUG)
	printf("[IDRM_SH_Update Gen] NodeId: %d updateType: %c updateId: %d dst: %d at %lf (from BaseAgent)\n",
	       myNodeId, idrm_rt_table[n_idrm_rt_entry].route_type, idrm_rt_table[n_idrm_rt_entry].updateId, 
	       idrm_rt_table[n_idrm_rt_entry].dst, CURR_TIME);
			
      n_idrm_rt_entry++;
    }//IDRM1 new change
    else if(prte->hop != idrm_rt->next_hop || prte->metric < idrm_rt->hop_count || prte->metric > idrm_rt->hop_count){ //next hop is changed 
      idrm_rt->recv_t					= prte->changed_at;
      idrm_rt->next_hop			= prte->hop;
      idrm_rt->hop_count			= prte->metric;
      idrm_rt->expire_t				= CURR_TIME + IDRM_ROUTE_EXPIRY;//prte->changed_at + IDRM_ROUTE_EXPIRY; //CURR_TIME + IDRM_ROUTE_EXPIRY;//
      idrm_rt->route_type			= 'U';
			
      memset(idrm_rt->path, -1, sizeof(nsaddr_t) *MAX_HOP);
      idrm_rt->path[0]				= prte->dst;//prte->hop;//0;
			
    }
    else if( prte->metric < 250 ){//&& !intra_rt_lookup(prte->dst)){			//found & valid
      idrm_rt->expire_t	= CURR_TIME + IDRM_ROUTE_EXPIRY;//prte->changed_at + IDRM_ROUTE_EXPIRY; //CURR_TIME + IDRM_ROUTE_EXPIRY;//
      idrm_rt->route_type			= 'U'; //0702
    }
  }
	
  if(IDRM_DEBUG)
    printf("NodeId:%d print BaseRT at RT exchange end(eIDRM <- Base) at %lf\n", myNodeId, CURR_TIME);
  dumpRT();
  dumpBaseRT();
	
  //IDRM_SH
}
void
IDRM::dsrRTReadTrackChanges(void){
}
void
IDRM::toraRTReadTrackChanges(void){
}

// =================================================
// Base routing agent routing table UPDATING routines -- give it a destination for lookup. It will set
// the passed nextIt will set
// the passed nextIt will set
// the passed next It will set
// the passed nexthop and check for "better" hopcount, expiry, sequencenumber and status
// return TCL_ERROR if it does not update any entry and does not add any entry. return TCL_OK if it updated or adds
int
IDRM::aodvRTUpdateEntryWithMyInterface(nsaddr_t dst, nsaddr_t nexthop, int hopcount, int seqnum){
  aodv_rt_entry *rt;
  rt = ((AODV *)baseRAgent)->rtable.rt_lookup(dst);
  if(rt) {
    // An entry already exists.
    // see if this entry needs NOT be updated
    if (  (rt->rt_seqno >= seqnum) ||  // Higher sequence means freasher
	  (CURR_TIME <= rt->rt_expire) || // it is fresh enough
	  (rt->rt_flags == RTF_UP) ||	 // it is active and UP where (RTF_DOWN=0,RTF_UP=1, RTF_IN_REPAIR=2)
	  (rt->rt_hops < hopcount) 
	  ){
      // need not update. So just return error
      return TCL_ERROR;
    }
    // the entry will be updated below after the "else" clause 
  } else {
    // "add" an entry
    rt = ((AODV *)baseRAgent)->rtable.rt_add(dst);
  }
	
  // By the time it comes here, either (i) an entry is added or (ii)old entry needs to be updated for destination "dst"
  // so we update the entry pointed to by rt
	
  rt->rt_seqno = seqnum;
  rt->rt_hops = hopcount;
  rt->rt_flags = RTF_UP;
  rt->rt_nexthop = nexthop;
  rt->rt_expire = CURR_TIME+IDRM_ROUTE_EXPIRY;
	
  // finally most imporatnt set my interface with a non "0" value i.e. not default interface
  rt->rt_interface = myIfaceId;
	
  // ALSO IMportant. Set the IDRM agent pointer in the base agent to point to me (this IDRM agent)
  // Then the base agent can use this IDRM agent's pointer to forward paackets when the interface is non-zero (not default)
	
  //((AODV *)baseRAgent)->idrmRAgent = (IDRM *) RAgent)->idrmRAgent = (IDRM *) this; //IDRM_SH move!
	
  return TCL_OK;
}

//IDRM_SH
int
IDRM::dsdvRTUpdateEntryWithMyInterface(nsaddr_t dst, nsaddr_t nexthop, int hopcount, int seqnum, nsaddr_t saddr){
	
  //Update rout table of the baseAgent
  //If update happens, trigger update msg sent to neighbor
  //RTUpdateEntryByIDRM(..) takes care of everything.
  ((DSDV_Agent*)baseRAgent)->RTUpdateEntryByIDRM(dst, nexthop, hopcount, seqnum, myIfaceId, saddr);
	
  //((DSDV_Agent *)baseRAgent)->idrmRAgent = (IDRM *) this; //IDRM_SH move!
  return TCL_OK;
}


int
IDRM::dsrRTUpdateEntryWithMyInterface(nsaddr_t dst, nsaddr_t nexthop, int hopcount, int seqnum){
  return TCL_ERROR;
}

int
IDRM::toraRTUpdateEntryWithMyInterface(nsaddr_t dst, nsaddr_t nexthop, int hopcount, int seqnum){
  return TCL_ERROR;
}


// Packet arrives here in this routine because the base routing agent wants to forward
// but the interface is not-default i.e. not 0. This implies only IDRM agent
// knows how to forward and it forwards it with (i) packet and (ii) delay to be added
// remeber we need to add IDRM jitter delay on top
void
IDRM::forwardFromBaseRoutingAgent(Packet *p_in, double delay) {
	
  if(IDRM_DEBUG)
    cout<<"forwardfrombaseroutingagent :"<<myNodeId<<endl;
	
  // add IDRM Jitter	
  delay = delay + IDRM_JITTER;
	
  struct hdr_cmn *ch_in  = HDR_CMN(p_in);
  struct hdr_ip *ih_in   = HDR_IP(p_in);
	
  if( ch_in->ptype() == PT_IDRM || ch_in->ptype() == PT_AODV){	//iIDRM pkt do not go outside, no encap for idrm packet
    Packet::free(p_in);
    return;
  }
  //if( ch_in->ptype() != PT_UDP	) return;	//iIDRM pkt do not go outside, no encap for idrm packet
	
  idrm_rt_entry* rt = rt_lookup(ih_in->daddr());
	
  //rt = rt_lookup(ih_in->daddr());
	
  dumpRT();
	
  if(rt == NULL){
    if(IDRM_DEBUG)
      printf("NodeId:%d No dst found in IDRM RT for %d at %lf\n", myNodeId, ih_in->daddr(), CURR_TIME);
    total_drop_pkts++;
    Packet::free(p_in);
    return;
  }
  else{
    if(IDRM_DEBUG)
      printf("rt is not NULL\n");
  }
	
  Packet *p;
	
  if(p_in->userdata())
    //p = Packet::alloc( ch_in->size() + sizeof(hdr_idrm) );
    p = Packet::alloc( ch_in->size() );
  else
    p = Packet::alloc();
	
  if(IDRM_DEBUG)	
    printf("new pk size=%d\n", ch_in->size() + sizeof(hdr_idrm));
	
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type				= IDRMTYPE_DATA;
  rh->fdst					= ih_in->daddr();
  rh->or_pkt_type		= ch_in->ptype();
  rh->or_size				= ch_in->size();
  rh->or_id					= ch_in->uid();
  rh->or_sport			= ih_in->sport();		//0723
  rh->or_dport			= ih_in->dport();
	
  ch->ptype() = PT_IDRM;
  ch->size() = ch_in->size() + sizeof(hdr_idrm);
  ch->iface() = -2;
  ch->error() = 0;
  ch->addr_type() = NS_AF_INET;//NS_AF_NONE;
  ch->prev_hop_ = myNodeId;		   // AODV hack
  ch->next_hop_ = rt->next_hop;
	
  ih->saddr() = ih_in->saddr();//myNodeId;
  ih->daddr() = rt->next_hop;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_ = ih_in->ttl_;
	
  if(p_in->userdata()) //check whether data is null
    memcpy(p->userdata(), p_in->userdata(), p_in->datalen()); //copy payload..
	
  if(IDRM_DEBUG)
    printf("[SH_TR] NodeId: %d eIDRM encap from: %d or:: to: %d ptype: %d pid: %d now:: to: %d ptype: %d pid: %d at %lf\n", 
	   myNodeId, ih->saddr(), rh->fdst, rh->or_pkt_type, rh->or_id, ih->daddr(), ch->ptype(), ch->uid(), CURR_TIME);
	
	
  sendData(p, delay);
	
  Packet::free(p_in);
	
  // Need to update the rh, ch and ih (like sendBeacon), modify the message type as it is go through IDRM
  // REMEMBER that the receive IDRM gateway tehn has to receive the message and either (i) forward it to another gateway or (ii) change
  // the message type to the base routing agent and forward it if it is internal to that domain
  // printf("[SH] IDRM_DEBUG RECEIVING FORWARD pkt_hdr(src=%d dst=%d idt_hdr(src=%d dst=%d id=%d if_id %d) on node %d at %lf on if_id %d\n", ih->saddr(), ih->daddr(), 
  //	ch->uid(), ch->iface(), myNodeId, CURR_TIME, myIfaceId);
}

void
IDRM::sendData(Packet *p, double delay) {
	
  if(IDRM_DEBUG)
    cout<<"senddata :"<<myNodeId<<endl;
	
  struct hdr_cmn *ch	= HDR_CMN(p);
  struct hdr_ip *ih	= HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p); 
	
  if(IDRM_DEBUG)
    printf("[SH IDRM] NodeId:%d send a pkt.. src:%d dst:%d nextHop:%d final_dst:%d TTL:%d, with delay:%lf\n", 
	   myNodeId, ih->saddr(), ih->daddr(), ch->next_hop_, rh->fdst, ih->ttl_, delay);
  //Scheduler::instance().schedule(target_, p, delay);	//later
	
  if(IDRM_DEBUG)
    printf("[SH_TR] NodeId: %d IDRM send from: %d to: %d ptype: %d pid: %d at %lf\n", 
	   myNodeId, ih->saddr(), ih->daddr(), ch->ptype(), ch->uid(), CURR_TIME);
  // Mission Policy
  // rh->domain = Domain;
	
  if(now_gateway){
    n_out_data ++;
    s_out_data += ch->size();
  }
  //cerr<<myNodeId<<" Sent data to "<<ih->daddr()<<endl;
  target_->recv(p, (Handler*) 0);
	
}

void
IDRM::recvData(Packet *p) {
	
  if(IDRM_DEBUG)
    cout<<"recvdata :"<<myNodeId<<endl;
	
  struct hdr_cmn *ch_in  = HDR_CMN(p);
  struct hdr_ip *ih_in   = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  n_in_data++;
  s_in_data += ch_in->size();
	
  if(IDRM_SH_DEMO_IDRM){
    if(ch_in->size() > 1000)
      //print my addr and next hop
      //<data time="1" src="0" dst="1" bytes="65431" />	
      //printf("<data time=\"%.0lf\" src=\"%d\" dst=\"%d\" bytes=\"%d\" />\n", CURR_TIME, myNodeId, ch->next_hop_, ch->size());
      //printf("<data time= %.0lf src= %d	 dst= %d  bytes= %d />\n", CURR_TIME, ih_in->saddr(), myNodeId, ch_in->size());
      printf("<data time= %.0lf src= %d  dst= %d	bytes= %d />\n", CURR_TIME, ch_in->prev_hop_, myNodeId, ch_in->size());
    //ch->prev_hop_
  }
	
  //if(myNodeId == 8 && rh->fdst == 11)
  //cerr<<"Wohoo packet reached "<<myNodeId<<" in recv Data"<<endl;
	
  //if(IDRM_DEBUG){
  //printf("[SH IDRM] NodeId:%d Data Received destinated to %d from %d(originally) %d(through) at %lf \n", 
  //myNodeId, rh->fdst, ih_in->saddr(), ch_in->prev_hop_, CURR_TIME);
  //printf("[SH_TR] NodeId: %d IDRM recv from: %d to: %d ptype: %d pid: %d at %lf\n", 
  //						myNodeId, ih_in->saddr(), ih_in->daddr(), ch_in->ptype(), ch_in->uid(), CURR_TIME);
  //}
	
  /*IDRM Agent received a packet
    Case1: Dst node is in the same MANET
    Then, IDRM agnet forwards the packet to the baseAgent
    First, packet type conversion....
	 
    Case2: Dst node is out of the same MANET
    See later.
	 
    Now assume, case1
  */
	
  //Creating a new packet
	
  Packet *new_p;
	
  if(p->userdata())
    new_p = Packet::alloc(rh->or_size);
  else
    new_p = Packet::alloc();
	
  struct hdr_cmn *ch	= HDR_CMN(new_p);
  struct hdr_ip *ih	= HDR_IP(new_p);
	
  ch->ptype()		= rh->or_pkt_type;
  ch->size()		= rh->or_size;
  ch->uid()				= rh->or_id;
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
  ch->direction() = hdr_cmn::DOWN;
	
  ih->saddr() = ih_in->saddr();
  ih->daddr() = rh->fdst;
  ih->sport() = rh->or_sport; //0723 RT_PORT;
  ih->dport() = rh->or_dport; //RT_PORT;
  ih->ttl_	= ih_in->ttl_;
	
  // Mission Policy
  if(ch->ptype() == PT_IDRM){						//if the pktype of the original pkt is IDRM, recover the msg type
    struct hdr_idrm *rh_in = HDR_IDRM(p);
    struct hdr_idrm *rh		 = HDR_IDRM(new_p);		
		
    rh->ih_type = rh_in->or_idrm_type;
		
  }
	
  if(p->userdata())
    memcpy(new_p->userdata(), p->userdata(), p->datalen()); //copy payload..
	
  if(IDRM_DEBUG)	
    printf("[SH_TR] NodeId: %d eIDRM decap from: %d or:: to: %d ptype: %d pid: %d now:: to: %d ptype: %d pid: %d at %lf\n", 
	   myNodeId, ih_in->saddr(), ih_in->daddr(), ch_in->ptype(), ch_in->uid(), ih->daddr(), ch->ptype(), ch->uid(), CURR_TIME);
	
	
  bool base_result = false;
	
  if (strcmp(baseRAgentName, "AODV") == 0){
		
    base_result = ((AODV *)baseRAgent)->from_diff_domain( ih->daddr() );
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
		
    base_result = ((DSDV_Agent*)baseRAgent)->from_diff_domain( ih->daddr() );
    //cerr<<"from_diff_domain called! base_result = "<<base_result<<endl;
  }
	
  if(!base_result){				//if dst is in the same domain...we count
    if(now_gateway){
      n_out_intra_data ++;
      s_out_intra_data += ch->size();
    }
  }
	
	
  Packet::free(p);
	
  ch->hop_count++;
	
  if(ch->ptype() == 2 && ih->daddr() == myNodeId){
    printf("[AGENT SH]IDRM NodeId: %d receiving UDP from %d pid: %d to %d hops: %d at %lf\n", 
	   myNodeId, ih->saddr(), ch->uid(), ih->daddr(), ch->hop_count, CURR_TIME);
    Packet::free(new_p);
    return;
  }
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM] NodeId:%d packet is forwarded to AODV from %d to %d ptype:%d\n", 
	     myNodeId, ih->saddr(), ih->daddr(), ch->ptype());
    //((AODV*)baseRAgent)->recv(new_p, 0);
    ((AODV*)baseRAgent)->forwardFromIDRM(new_p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
		
    //cerr<<"Enetered to forard pcket done by node = "<<myNodeId<<endl;
    if(IDRM_DEBUG)	
      printf("[SH IDRM] NodeId:%d packet is forwarded to DSDV from %d to %d ptype:%d\n",myNodeId, ih->saddr(), ih->daddr(), ch->ptype());		
    ((DSDV_Agent*)baseRAgent)->forwardPacket(new_p);
  }
  else if (strcmp(baseRAgentName, "DSR") == 0){
		
  }
  else if (strcmp(baseRAgentName, "TORA") == 0){
		
  }
  else {
    printf("IDRM_ERROR at recvData(): Unknown Base routing agent on Node %d at %lf\n", myNodeId, CURR_TIME);
    exit(1);
  }
}

//used by baseRAgent to decide whether a beacon is from gateway or not
bool
IDRM::isFromGateway(nsaddr_t src){
	
  for(int i=0; i<num_intra_gateway; i++){
    if( intra_gateways[i] == src ) return true;		
  }
  return false;	
}

// Base - IDRM
// ======================================================================

// ======================================================================
// Util

void
IDRM::remove_route(int index) {
	
  if(IDRM_DEBUG)
    cout<<"remove_route :"<<myNodeId<<endl;
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG REMOVE ROUTE %d at node %d at %lf \n", index, myNodeId, CURR_TIME);
	
  dumpRT();
	
  if( n_idrm_rt_entry == 0 ) 
    return;
	
  if( index == n_idrm_rt_entry -1 ) {
    n_idrm_rt_entry--;
  }
  else {
    memcpy( &(idrm_rt_table[index]), &(idrm_rt_table[n_idrm_rt_entry-1]), sizeof(struct idrm_rt_entry));
    n_idrm_rt_entry--;
  }
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG Final RT at node %d at %lf \n", myNodeId, CURR_TIME);
	
  dumpRT();
}


void
IDRM::remove_intra_route(int index) {
	
  if(IDRM_DEBUG)
    cout<<"remove_intra_route :"<<myNodeId<<endl;
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG REMOVE Intra ROUTE %d at node %d (# of entries:%d)at %lf \n", index, myNodeId, n_intra_idrm_rt_entry, CURR_TIME);
  dumpIntraRT();
	
  if( n_intra_idrm_rt_entry == 0 ) 
    return;
	
  if( index == n_intra_idrm_rt_entry -1 ) {
    n_intra_idrm_rt_entry--;
  }
  else {
    memcpy( &(intra_idrm_rt_table[index]), &(intra_idrm_rt_table[n_intra_idrm_rt_entry-1]), sizeof(struct idrm_rt_entry));
    n_intra_idrm_rt_entry--;
  }
	
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG Final Intra RT at node %d at %lf \n", myNodeId, CURR_TIME);
  dumpIntraRT();
}

void
IDRM::dumpRT() {
  if(SH_DUMP){
    int i,j;
    printf("IDRM_DEBUG Dump eIDRM Route Table for Node %d at %.4lf [%d %d %d]\n", 
	   myNodeId, CURR_TIME, n_out_beacon, n_out_update, n_out_table);
    for( i=0; i<n_idrm_rt_entry; i++) {
      printf("At %lf %3d type %c	myid:%d dst %d hop %d next %d expire %.4lf",
	     CURR_TIME, i, idrm_rt_table[i].route_type, myNodeId,
	     idrm_rt_table[i].dst, 
	     idrm_rt_table[i].hop_count,
	     idrm_rt_table[i].next_hop,
	     idrm_rt_table[i].expire_t);
      printf("path ");
      for(j=0; j < idrm_rt_table[i].hop_count; j++) {
	if(j >= MAX_HOP){break;} //0320
	printf(" (%d)", idrm_rt_table[i].path[j]);
      }
      printf("\n");
    }
  }
}

void
IDRM::dumpIntraRT() {
  if(SH_DUMP){
    int i,j;
    printf("IDRM_DEBUG Dump iIDRM Route Table for Node %d at %.4lf [%d %d %d]\n", 
	   myNodeId, CURR_TIME, n_out_intra_beacon, n_out_intra_update, n_out_intra_table);
    for( i=0; i<n_intra_idrm_rt_entry; i++) {
      printf("%3d type %c dst %d hop %d next %d expire %.4lf ",
	     i, intra_idrm_rt_table[i].route_type,
	     intra_idrm_rt_table[i].dst, 
	     intra_idrm_rt_table[i].hop_count,
	     intra_idrm_rt_table[i].next_hop,
	     intra_idrm_rt_table[i].expire_t);
      printf("path ");
      for(j=0; j < intra_idrm_rt_table[i].hop_count; j++) {
	if(j >= MAX_HOP){break;} //0320
	printf(" %d", intra_idrm_rt_table[i].path[j]);
      }
      printf("\n");
    }
  }
}


void
IDRM::dumpBaseRT(){
  if(SH_DUMP){
    int count=0;
		
    if (strcmp(baseRAgentName, "AODV") == 0){
			
      aodv_rt_entry *rt;
      printf("IDRM_DEBUG AODV BEGIN Routing Table Read from Base Agent on Node %d at %lf\n",myNodeId, CURR_TIME);
			
      for(rt = ((AODV *)baseRAgent)->rtable.head(); rt; rt = rt->rt_link.le_next) {
	printf("AODV ROUTE ENTRY: (Dest,NextHop,HopCount,Iface, flag, seq, expire_t)= myid: %d (%d,%d,%d,%d,%d,%d, %lf) at %lf\n",myNodeId,rt->rt_dst, rt->rt_nexthop,
	       rt->rt_hops, rt->rt_interface, rt->rt_flags, rt->rt_seqno, rt->rt_expire, CURR_TIME);
	count++;
      }
			
      printf("IDRM_DEBUG AODV END (%d entries)=====================>\n",count);
    }
    else if (strcmp(baseRAgentName, "DSDV") == 0){
			
      RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
      rtable_ent *prte;
			
      printf("IDRM_DEBUG DSDV BEGIN Routing Table Read from Base Agent on Node %d at %lf\n",myNodeId, CURR_TIME);
			
      for(rt->InitLoop(); (prte = rt->NextLoop()); ){
	if( prte->metric != 250 ){
	  printf("DSDV ROUTE ENTRY: (Dest, NextHop, HopCount, Iface, changed_t) =myid: %d (%d,%d,%d,%d, %lf)\n",
		 myNodeId, prte->dst, prte->hop, prte->metric, prte->rt_interface, prte->changed_at);
	  count++;
	}
      }
      printf("IDRM_DEBUG DSDV END (%d entries)=====================>\n",count);
    }
  }
}

void
IDRM::dumpPacket(Packet * p) {
  if(IDRM_DEBUG)
    printf("IDRM_DEBUG Dump Packet at Node %d at %.4lf\n", myNodeId, CURR_TIME);
	
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  if(IDRM_DEBUG)
    printf("Type %d ", rh->ih_type);
	
  if(IDRM_DEBUG)
    printf("\n");
}

// ======================================================================
// Packet Processing Functions

void
IDRM::recv(Packet *p, Handler *)
{
  if(IDRM_DEBUG)
    cout<<"recv :"<<myNodeId<<endl;
  /*
    if( (float) CURR_TIME > 500 &&	 (float) CURR_TIME < 1500 ){
    IDRM_DEBUG = 1;
    SH_DUMP =1;
    }
    else{
    IDRM_DEBUG = 0;
    SH_DUMP =0;
    }
  */
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih  = HDR_IP(p);
  struct hdr_idrm *rh =HDR_IDRM(p);
  //printf("AODV RT TABLE: %d's at curr_time: %d\n",myNodeId,CURR_TIME);
  //printf("IDRM_AGENT NodeId: %d recv pid:%d(%d) from %d target:%s at %lf\n", myNodeId, ch->uid(), ch->ptype(), ih->saddr(),this->name(), CURR_TIME);
	
  assert(initialized());
  if(rh->ih_type == IDRMTYPE_DATA)
    printf("IDRM_DEBUG receiving pkt_hdr(src=%d dst=%d id=%d if_id %d) from node %d at %lf on if_id %d ch_type:%d\n",
	   ih->saddr(), ih->daddr(), 
	   ch->uid(), ch->iface(), myNodeId, CURR_TIME, myIfaceId,
	   ch->ptype());
	
  /*if(rh->ih_type == IDRMTYPE_DATA && Policy && Domain == 'A' && rh->Domain == 'B')
    {
    return;
    }*/
	
  //if((Policy && myNodeId == 2 && rh->domain == 'B'))
  //	{
  //if(rh->ih_type==IDRMTYPE_DATA)
  //cerr<<"Dropping data packet from untrusted domain! at "<<CURR_TIME<<endl;
  //else 
  //cerr<<"Dropping beacon packet from untrusted domain! at "<<CURR_TIME<<endl;				
  //				return;
  //}
	
  if(IDRM_DEBUG)	
    printf("IDRM_DEBUG receiving pkt_hdr(src=%d dst=%d id=%d if_id %d) from node %d at %lf on if_id %d ch_type:%d rh->type%d\n",
	   ih->saddr(), ih->daddr(), 
	   ch->uid(), ch->iface(), myNodeId, CURR_TIME, myIfaceId,
	   ch->ptype(), rh->ih_type);
	
	
  if (baseRAgent == NULL){
    printf("IDRM_ERROR Receiving packet when baseagent is empty...in node %d pkt_if %d if %d,%d at time %lf\n", 
	   myNodeId, ch->iface_, myIfaceId, myIface, CURR_TIME);
    exit(1);
  }
  else {
    // reorganized the classifier (defaulttarget) so as to come to IDRM agent first if it exists. I
    // DRM reagent will redirect appropriately
		
    if(IDRM_SH_DEMO_IDRM){
      if(ch->size() > 1000)
	//print my addr and next hop
	//<data time="1" src="0" dst="1" bytes="65431" />	
	//printf("<data time=\"%.0lf\" src=\"%d\" dst=\"%d\" bytes=\"%d\" />\n", CURR_TIME, myNodeId, ch->next_hop_, ch->size());
	//printf("<data time= %.0lf src= %d	 dst= %d  bytes= %d />\n", CURR_TIME, ih_in->saddr(), myNodeId, ch_in->size());
	printf("<data time= %.0lf src= %d  dst= %d	bytes= %d />\n", CURR_TIME, ch->prev_hop_, myNodeId, ch->size());
    }
		
    //printf("Bf NodeId: %d pkt is IDRM(src:%d, dst:%d) but not for me\n", myNodeId, ih->saddr(), ih->daddr());
		
    if(!now_gateway && ch->ptype() == PT_IDRM && rh->ih_type == IDRMTYPE_INTRA_DATA)
      {
	if(IDRM_DEBUG)
	  printf("NodeId: %d now Im not active, pkt is forwarded to base\n",myNodeId,CURRENT_TIME);
	baseRAgent->recv(p, (Handler*)0);
	return;
      }
    if(ch->ptype() == PT_IDRM && ih->daddr() != -1 && ih->daddr() != myNodeId && rh->ih_type != IDRMTYPE_INTRA_DATA && rh->ih_type != IDRMTYPE_DATA){
      baseRAgent->recv(p, (Handler*) 0);
      return;
    }	
		
    if(ch->ptype() != PT_IDRM) {
      // if it is not any IDRM stuff pass it back to baseagent  like AODV/DSR/DSDV/TORA
      //if(IDRM_DEBUG)	
      //printf("pkt is forwarded to base\n");
      //cerr<<"pkt forwarded to base: by: "<<myNodeId<<endl;
      baseRAgent->recv(p, (Handler*) 0);
			
      //			idrm_base->recv(p, (Handler*) 0);
      return;
    }
		
    // Now process IDRM message
    assert(ih->sport() == RT_PORT);
    assert(ih->dport() == RT_PORT);
		
    //do not process eIDRM packet from intra-gateways: 0812
    if( rh->ih_type <= 0x04 && intra_lookup(ih->saddr()) ){
      Packet::free(p);
      return;
    }
		
    //0311	 
    /*
      if(!now_gateway){
      Packet::free(p);
      return;	
      }
    */
		
    if( ih->saddr() == myNodeId){
      Packet::free(p);
      return;
    }
		
    switch(rh->ih_type) {
    case IDRMTYPE_BEACON:								
      recvBeacon(p);
      break;
    case IDRMTYPE_ROUTE_TABLE_REQUEST:	
      recvRouteTableRequest(p);
      break;
    case IDRMTYPE_ROUTE_TABLE_REPLY:		
      recvRouteTable(p);
      break;
    case IDRMTYPE_ROUTE_UPDATE:					
      recvRouteUpdate(p);
      break;
    case IDRMTYPE_DATA:
      //if(myNodeId==8)
      //cerr<<"Received data packet MyNodeId = "<<myNodeId<<" now_gateway = "<<now_gateway<<endl;									
      recvData(p);
      break;
    case IDRMTYPE_INTRA_BEACON:		
      recvIntraBeacon(p);
      break;
    case IDRMTYPE_INTRA_ROUTE_TABLE_REQUEST:
      recvIntraRTRequest(p);
      break;
    case IDRMTYPE_INTRA_ROUTE_REPLY:
      recvIntraRTReply(p);
      break;
    case IDRMTYPE_INTRA_ROUTE_UPDATE:
      recvIntraRTUpdate(p);
      break;
    case IDRMTYPE_INTRA_DATA:
      //if(myNodeId==11)
      //cerr<<"Received Intra data packet MyNodeId = "<<myNodeId<<" now_gateway = "<<now_gateway<<endl;
      recvIntraData(p);
      break;
    case IDRMTYPE_INTRA_NGT:	//0323
      recvIntraNGT(p);
      break;		
    case IDRMTYPE_INTRA_GT_TURN_OFF:	//0324
      recvInrraGatewayTurnOff(p);
      break;
    default:
      printf("IDRM_ERROR: Invalid IDRM type (%x), Receiving(from:%d) from layer BELOW...in node %d pkt_if %d if %d,%d at time %lf\n",
	     rh->ih_type, ih->saddr(), myNodeId, ch->iface_, myIfaceId,myIface, CURR_TIME);
      exit(1);
    }
  }
}

// ======================================================================
// Util:

//IDRM_SH
idrm_rt_entry*
IDRM::rt_lookup(nsaddr_t dst){
	
  //for(int i=0; i<MAX_ROUTE; i++){
  for(int i=0; i<n_idrm_rt_entry; i++){
    if(idrm_rt_table[i].dst == dst)
      return &idrm_rt_table[i];
  }
  return NULL;	
}		
//IDRM_SH


char*
IDRM::itoa(int val)
{
  int base = 10;
  static char buf[32] = {0};
	
  int i = 30;
	
  for(; val && i ; --i, val /= base)
		
    buf[i] = "0123456789abcdef"[val % base];
	
  return &buf[i+1]; 
}

void
IDRM::setMANET_ID(){
	
}

void
IDRM::idrm_result_record(){
	
  //idrm_output = fopen(fname, "aw");
	
  //for outgoing packets..
  n_total_out_eidrm	+= (n_out_beacon + n_out_request + n_out_table + n_out_update);
  n_total_out_iidrm	+= (n_out_intra_beacon + n_out_intra_request + n_out_intra_table + n_out_intra_update);
  n_total_out_data	+= (n_out_data + n_out_intra_data);
	
  s_total_out_eidrm	+= (s_out_beacon + s_out_request + s_out_table + s_out_update);
  s_total_out_iidrm	+= (s_out_intra_beacon + s_out_intra_request + s_out_intra_table + s_out_intra_update);
  s_total_out_data	+= (s_out_data + s_out_intra_data);
	
  //for incoming packets...
  n_total_in_eidrm	+= (n_in_beacon + n_in_request + n_in_table + n_in_update);
  n_total_in_iidrm	+= (n_in_intra_beacon + n_in_intra_request + n_in_intra_table + n_in_intra_update);
  n_total_in_data += (n_in_data + n_in_intra_data);
	
  s_total_in_eidrm	+= (s_in_beacon + s_in_request + s_in_table + s_in_update);
  s_total_in_iidrm	+= (s_in_intra_beacon + s_in_intra_request + s_in_intra_table + s_in_intra_update);
  s_total_in_data += (s_in_data + s_in_intra_data);
	
  printf("[IDRM_OVERHEAD] NodeId: %d %10lf eBeacon_out: %d %d eRTRequest_out: %d %d eRTReply_out: %d %d eRTUpdate_out: %d %d eData_out: %d %d iBeacon_out: %d %d iRTRequest_out: %d %d iRTReply_out: %d %d iRTUpdate_out: %d %d iData_out: %d %d total_eIDRM_out: %d %d total_iIDRM_out: %d %d total_data_out: %d %d ",
	 myNodeId,
	 CURR_TIME,
	 n_out_beacon, s_out_beacon, n_out_request, s_out_request, n_out_table, s_out_table, n_out_update, s_out_update, n_out_data, s_out_data,
	 n_out_intra_beacon, s_out_intra_beacon, n_out_intra_request, s_out_intra_request, n_out_intra_table, s_out_intra_table, n_out_intra_update, s_out_intra_update, n_out_intra_data, s_out_intra_data,
	 n_total_out_eidrm, s_total_out_eidrm, n_total_out_iidrm, s_total_out_iidrm, n_total_out_data, s_total_out_data);
	
  printf(" eBeacon_in: %d %d eRTRequest_in: %d %d eRTReply_in: %d %d eRTUpdate_in: %d %d eData_in: %d %d iBeacon_in: %d %d iRTRequest_in: %d %d iRTReply_in: %d %d iRTUpdate_in: %d %d iData_in: %d %d total_eIDRM_in: %d %d total_iIDRM_in: %d %d total_data_in: %d %d\n",
	 n_in_beacon, s_in_beacon, n_in_request, s_in_request, n_in_table, s_in_table, n_in_update, s_in_update, n_in_data, s_in_data,
	 n_in_intra_beacon, s_in_intra_beacon, n_in_intra_request, s_in_intra_request, n_in_intra_table, s_in_intra_table, n_in_intra_update, s_in_intra_update, n_in_intra_data, s_in_intra_data,
	 n_total_in_eidrm, s_total_in_eidrm, n_total_in_iidrm, s_total_in_iidrm, n_total_in_data, s_total_in_data);
	
	
  n_total_iidrm_pkt += n_total_out_iidrm;
  s_total_iidrm_pkt += s_total_out_iidrm;
	
  n_total_eidrm_pkt += n_total_out_eidrm;
  s_total_eidrm_pkt += s_total_out_eidrm;
	
  //printf("[IDRM_OVERHEAD] NodeId: %d n_iidrm: %d s_iidrm: %d n_eidrm: %d s_eidrm: %d at %lf\n", myNodeId, n_total_iidrm_pkt, s_total_iidrm_pkt, n_total_eidrm_pkt, s_total_eidrm_pkt, CURRENT_TIME);
	
  //fclose(idrm_output);
	
  printf("[IDRM GATEWAY] NodeId: %d idrm_gateway: %d at %lf\n", myNodeId, now_gateway, CURR_TIME);
  stat_reset();
	
  if(myNodeId == 0){ //only node 0 record
    printf("[IDRM_SH GT_COUNTER] %d at %lf\n", gateway_counter, CURR_TIME);
    gateway_counter=0; //reset
    //printf("[IDRM_OVERHEAD] NodeId: %d n_iidrm: %d s_iidrm: %d n_eidrm: %d s_eidrm: %d at %lf\n", myNodeId, n_total_iidrm_pkt, s_total_iidrm_pkt, n_total_eidrm_pkt, s_total_eidrm_pkt, CURRENT_TIME);
		
    n_total_iidrm_pkt =0;
    s_total_iidrm_pkt =0;
		
    n_total_eidrm_pkt =0;
    s_total_eidrm_pkt =0;
  }
	
  if(now_gateway){ //only record when I'm gateway
    rt_count++;
    printf("[IDRM_SH RT] NodeId:%d, Routes: %d at %lf counter: %d\n", myNodeId, n_idrm_rt_entry, CURR_TIME, rt_count);
  }	
	
  //for sid 0406
  if (IDRM_DEBUG && strcmp(baseRAgentName, "DSDV") == 0){
    printf("[SID] NodeId: %d #ofDSDVRTs: %d hops: %f at %lf\n", myNodeId, base_lookup(), base_hop_count(), CURR_TIME);
    printf("[DSDV_Single] NodeId: %d #of1hops: %d at %lf\n", myNodeId, dsdv_single_hops(), CURR_TIME);
  }
	
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    ((AODV *)baseRAgent)->printOverhead();
  }
	
  //for verification 0407
if(IDRM_DEBUG){
  if(identity)
    printf("[VERIFICATION] NodeId: %d #ofIDRM: %d gateway: %d at %lf\n", myNodeId, n_idrm_rt_entry, now_gateway, CURR_TIME);
	
  if(identity)
    printf("eNC NodeId: %d numOfNeighbors: %d at: %lf\n", myNodeId, inter_neighbors_counter, CURR_TIME);
	
  if(identity && now_gateway){
    printf("eNCG NodeId: %d numOfInterNeighbors: %d at: %lf\n", myNodeId, inter_neighbors_counter, CURR_TIME);
}		
    //0824
    int intra_gts=0;
    for(int i=0; i< num_intra_gateway; i++){
      if( intra_neighbors_ts[i] > 0 && intra_neighbors_ts[i] < CURR_TIME ){
	intra_gts++;
      }
    }
		
    //printf("iNCG NodeId: %d numOfIntraNeighbors: %d at: %lf\n", myNodeId, intra_gts, CURR_TIME);
  }
	
  inter_neighbors_counter=0;
  memset(inter_neighbors, 0, sizeof(bool)*num_intra_gateway);
	
}

bool
IDRM::from_diff_domain(nsaddr_t dst){
	
  bool result = true;
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    aodv_rt_entry *rt;
		
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( dst );
    if( rt && rt->rt_interface == 0 && rt->rt_hops != 255 ) result = false;
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    rtable_ent *prte;
    RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
		
    prte = rt->GetEntry( dst ); //get an entry
    if( prte && prte->rt_interface == 0 && prte->metric != 250) result = false;
  }
  else if (strcmp(baseRAgentName, "DSR") == 0){
		
  }
  else if (strcmp(baseRAgentName, "TORA") == 0){
  }
  else{
    printf("IDRM_ERROR: Unknown Base routing agent on Node %d at %lf\n",myNodeId, CURR_TIME);
    exit(1);
  }
	
  return result;		  
}

int
IDRM::rt_idrm_lookup(){
  //return the number of external entries
	
  int count =0;
  for(int i=0; i<n_idrm_rt_entry; i++){
		
    int dst = idrm_rt_table[i].dst;
		
    if (strcmp(baseRAgentName, "AODV") == 0){
      aodv_rt_entry *rt;
			
      rt = ((AODV *)baseRAgent)->rtable.rt_lookup( dst );
      if( rt && rt->rt_interface == 0 ) count++;
    }
    else if (strcmp(baseRAgentName, "DSDV") == 0){
      rtable_ent *prte;
      RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
			
      prte = rt->GetEntry( dst ); //get an entry
      if( prte && prte->rt_interface == 0 ) count++;
    }	
    else if (strcmp(baseRAgentName, "DSR") == 0){
    }
    else if (strcmp(baseRAgentName, "TORA") == 0){
    }
    else{
      printf("IDRM_ERROR: Unknown Base routing agent on Node %d at %lf\n",myNodeId, CURR_TIME);
      exit(1);
    }
  }
	
  //printf("[IDRM_SH]NodeId:%d #ofexternal:%d at %lf\n", myNodeId, n_idrm_rt_entry - count, CURR_TIME);
	
  return (n_idrm_rt_entry - count);
	
}

int
IDRM::gateway_lookup(nsaddr_t dst){
	
  for(int i=0; i<num_intra_gateway; i++){
    if( intra_gateways[i] == dst ) 
      return i;		
  }
	
  return -1;	
}

bool
IDRM::intra_lookup(nsaddr_t dst){
	
  bool base_result = false;
	
  if (strcmp(baseRAgentName, "AODV") == 0){
		
    base_result = ((AODV *)baseRAgent)->from_diff_domain( dst );
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    base_result = ((DSDV_Agent*)baseRAgent)->from_diff_domain( dst );
  }
	
  if(!base_result)
    return true;
  else
    return false;
}

// Util: Regardless IDRM
// ======================================================================


idrm_rt_entry*
IDRM::intra_rt_lookup(nsaddr_t dst){
	
  for(int i=0; i<n_intra_idrm_rt_entry; i++){
		
    if(intra_idrm_rt_table[i].dst == dst)
      return &intra_idrm_rt_table[i]; 
  }
  return NULL;
}



void
IDRM::intra_encapsulation(Packet * p_in){
  /*
    todo: do encapsulation and forward the encapsulated packet to baseAgent
  */
  if(IDRM_DEBUG)
    cout<<"intra_encapsulation :"<<myNodeId<<endl;
  // add IDRM Jitter	
  double delay = delay + IDRM_JITTER; //later
	
  struct hdr_cmn *ch_in  = HDR_CMN(p_in);
  struct hdr_ip *ih_in   = HDR_IP(p_in);
	
	
  if( ch_in->ptype() == PT_IDRM ){
    Packet::free(p_in);
    return; //no encap for idrm packet
  }
	
  //if baseAgent does not have dst info, cannot do encapsulation	
  //check if baseAgent
	
  //if(!base_lookup(ih_in->daddr())){
	
  if(!base_lookup(intra_rt_lookup(ih_in->daddr())->next_hop)){
    if(IDRM_DEBUG)	
      printf("[IDRM_SH]NodeId:%d baseAgent does not have the entry for %d(next:%d), cannot do encapsulation\n", 
	     myNodeId, ih_in->daddr(), intra_rt_lookup(ih_in->daddr())->next_hop);
    dumpBaseRT();
    Packet::free(p_in);																						
    return;
  }
	
  nsaddr_t next_hop =-1;
  if (strcmp(baseRAgentName, "AODV") == 0){
    aodv_rt_entry *rt;
		
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( intra_rt_lookup(ih_in->daddr())->next_hop );
    if( rt && rt->rt_flags == RTF_UP) next_hop = rt->rt_nexthop;
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    rtable_ent *prte;
    RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
		
    prte = rt->GetEntry( intra_rt_lookup(ih_in->daddr())->next_hop ); //get an entry
    if( prte && prte->metric != 250 ) next_hop = prte->hop;
  }
	
  if(next_hop < 0){
    if(IDRM_DEBUG)	
      printf("[IDRM_SH]NodeId:%d invalid next_hop\n", myNodeId);
    Packet::free(p_in);
    return; 
  }
	
  Packet *p;
  if(p_in->userdata())
    p = Packet::alloc(ch_in->size() + sizeof(hdr_idrm));
  else 
    p = Packet::alloc();
	
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type			= IDRMTYPE_INTRA_DATA;
  rh->fdst			= ih_in->daddr();					//store the final/original dst
  rh->or_pkt_type = ch_in->ptype();					//store the original ptype
  rh->or_size			= ch_in->size();
  rh->or_id				= ch_in->uid(); 
  rh->or_sport		= ih_in->sport();		//0723
  rh->or_dport		= ih_in->dport();
  rh->or_hops			= ch_in->hop_count; //0824
	
  ch->ptype() = PT_IDRM;
  ch->size() = ch_in->size() + sizeof(hdr_idrm);
  ch->iface() = -2;
  ch->error() = 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_ = myNodeId;		   // AODV hack
  ch->next_hop_ = next_hop;
  ch->direction() = hdr_cmn::DOWN;
	
  ch->hop_count		= ch_in->hop_count;
	
  ih->saddr() = ih_in->saddr();
  ih->daddr() = intra_rt_lookup(ih_in->daddr())->next_hop;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_ = ih_in->ttl_;
	
  if(p_in->userdata()){ //check whether data is null
    memcpy(p->userdata(), p_in->userdata(), p_in->datalen()); //copy payload..
  }
  else{
    if(IDRM_DEBUG)	
      printf("data null\n");
  }
  //packet is encapsulated now..forward the new packet to baesAgent
	
  if(IDRM_DEBUG)	
    printf("[SH IDRM] NodeId: %d encapsulated packet is forwarded to baseRAgent (or: from %d to %d pid: %d ) (now: from %d to %d next: %d pid: %d) at %lf\n", 
	   myNodeId, ih_in->saddr(), rh->fdst, rh->or_id, ih->saddr(), ih->daddr(), ch->next_hop_, ch->uid(), CURR_TIME);
  if(IDRM_DEBUG)	
    printf("[SH_TR] NodeId: %d iIDRM encap from: %d or:: to: %d ptype: %d pid: %d now:: to: %d ptype: %d pid: %d at %lf\n", 
	   myNodeId, ih->saddr(), rh->fdst, rh->or_pkt_type, rh->or_id, ih->daddr(), ch->ptype(), ch->uid(), CURR_TIME);
	
  if(now_gateway){
    n_out_intra_data ++;
    s_out_intra_data += ch->size();
  }
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    ((DSDV_Agent*)baseRAgent)->forwardPacket(p);
  }
	
  Packet::free(p_in);
}


void
IDRM::recvIntraData(Packet *p){
  /*
    todo: decapsulation then forward the decapsulated packet to the baseAgent
  */
	
  if(IDRM_DEBUG) 
    cout<<"recvintradata - deccapsulation :"<<myNodeId<<endl;
	
  struct hdr_cmn *ch_in  = HDR_CMN(p);
  struct hdr_ip *ih_in   = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  n_in_intra_data++;
  s_in_intra_data += ch_in->size();
	
  Packet *new_p;
	
  if(p->userdata())
    new_p = Packet::alloc(rh->or_size);
  else
    new_p = Packet::alloc();
	
  struct hdr_cmn *ch	= HDR_CMN(new_p);
  struct hdr_ip *ih	= HDR_IP(new_p);
	
  ch->ptype()		= rh->or_pkt_type;
  ch->size()		= rh->or_size;
  ch->uid()				= rh->or_id;
  ch->iface()		= -2;
  ch->error()		= 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_	= myNodeId;			 // AODV hack
	
  ch->hop_count		= rh->or_hops;
	
  ih->saddr() = ih_in->saddr();
  ih->daddr() = rh->fdst;
  ih->sport() = rh->or_sport; //RT_PORT;
  ih->dport() = rh->or_dport; //RT_PORT;
  ih->ttl_	= ih_in->ttl_;
	
	
  if(ch->ptype() == PT_IDRM){						//if the pktype of the original pkt is IDRM, recover the msg type
    struct hdr_idrm *rh_in = HDR_IDRM(p);
    struct hdr_idrm *rh		 = HDR_IDRM(new_p);		
		
    rh->ih_type = rh_in->or_idrm_type;
		
  }
	
	
  if(p->userdata())
    memcpy(new_p->userdata(), p->userdata(), p->datalen()); //copy payload..
	
  if(IDRM_DEBUG)	
    printf("or size:%d size_was:%d data_len:%d\n", ch->size(), ch_in->size(), p->datalen());
	
  if(IDRM_DEBUG)
    printf("[SH IDRM] NodeId: %d packet is decapsulated to ptype: %d pid: %d and forwarded to baseRAgent (or: from %d to %d) (was: from %d to %d) at %lf\n", 
	   myNodeId, ch->ptype(), ch->uid(), ih->saddr(), ih->daddr(), ih_in->saddr(), ih_in->daddr(), CURR_TIME);
	
  if(IDRM_DEBUG)	
    printf("before forwarding src:%d dst:%d\n", ih->saddr(), ih->daddr());
	
  if(IDRM_DEBUG)	
    printf("[SH_TR] NodeId: %d iIDRM decap from: %d or:: to: %d ptype: %d pid: %d now:: to: %d ptype: %d pid: %d at %lf\n", 
	   myNodeId, ih->saddr(), ih_in->daddr(), ch_in->ptype(), ch_in->uid(), ih->daddr(), ch->ptype(), ch->uid(), CURR_TIME);
	
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    ((AODV*)baseRAgent)->forwardFromIDRM(new_p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    ((DSDV_Agent*)baseRAgent)->forwardPacket(new_p);
  }
	
  Packet::free(p);
}

void
IDRM::sendIntraNGT(nsaddr_t dst, int id){
	
  if(identity == false) return; //0404
	
  if(election_algorithm == ALGORITHM_STATIC) return; //static case
  if( dst == -1 ) return; //invalid address
	
	
  Packet *p = Packet::alloc();
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type = IDRMTYPE_INTRA_NGT;
	
  ch->uid() = beaconId++;
  ch->ptype() = PT_IDRM;
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm);
  ch->iface() = -2;
  ch->error() = 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_ = myNodeId;		   // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_ = NETWORK_DIAMETER;
	
  //printf("[IDRM_SH] NodeId:%d sending Intra Beacon() to %d at %lf\n ", myNodeId, ih->daddr(), CURR_TIME); 
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to AODV] NodeId:%d sent Intra NGT to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to DSDV] NodeId:%d sent Intra NGT to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
  }else{
    Packet::free(p);
  }
	
  //n_out_intra_beacon ++;
  //s_out_intra_beacon += ch->size();
	
	
}

void
IDRM::recvIntraNGT(Packet *p){
	
  struct hdr_ip *ih = HDR_IP(p);
	
  int src = ih->saddr();
  int index = getIndex(src);
  intra_neighbors_update[index] = false; //set false to prevent sending intraRTU
	
  //printf("0323 NodeId: %d recvIntraNGT(%d) from %d(index:%d) at %lf\n", myNodeId, ch->uid(), src, index, CURR_TIME);
	
  Packet::free(p);
}


//0324	
void
IDRM::sendIntraGatewayTrunOff(nsaddr_t dst){
	
  if(identity == false) return; //0404
	
  if(election_algorithm == ALGORITHM_STATIC) return; //static case
  if( dst == -1 ) return; //invalid address
	
  Packet *p = Packet::alloc();
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
  struct hdr_idrm *rh = HDR_IDRM(p);
	
  rh->ih_type = IDRMTYPE_INTRA_GT_TURN_OFF;
  rh->my_gt_route = after_selection_my_routes;  //0324
	
  ch->uid() = beaconId++;
  ch->ptype() = PT_IDRM;
  ch->size() = IP_HDR_LEN + sizeof(hdr_idrm);
  ch->iface() = -2;
  ch->error() = 0;
  ch->addr_type() = NS_AF_NONE;
  ch->prev_hop_ = myNodeId;		   // AODV hack
	
  ih->saddr() = myNodeId;
  ih->daddr() = dst;
  ih->sport() = RT_PORT;
  ih->dport() = RT_PORT;
  ih->ttl_ = NETWORK_DIAMETER;
	
  printf("0324 NodeId: %d turnOffGT to %d(pid %d) at %lf\n", myNodeId, dst, ch->uid(), CURR_TIME);
	
  //printf("[IDRM_SH] NodeId:%d sending Intra Beacon() to %d at %lf\n ", myNodeId, ih->daddr(), CURR_TIME); 
  if (strcmp(baseRAgentName, "AODV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to AODV] NodeId:%d sent Intra GT TunrOff to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((AODV*)baseRAgent)->forwardFromIDRM(p);
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    if(IDRM_DEBUG)	
      printf("[SH IDRM to DSDV] NodeId:%d sent Intra GT TunrOff to %d at %lf\n", myNodeId, dst, CURR_TIME);
    ((DSDV_Agent *)baseRAgent)->forwardPacket(p);
  }else{
    Packet::free(p);
  }
}

void
IDRM::recvInrraGatewayTurnOff(Packet *p){
	
  struct hdr_cmn *ch = HDR_CMN(p);
  struct hdr_ip *ih = HDR_IP(p);
	
  if(now_gateway == true){
		
    printf("0324 NodeId: %d gt->ngt recvGTTurnOff from %d(pid: %d) at %lf\n", myNodeId, ih->saddr(), ch->uid(), CURR_TIME);
    now_gateway = false;
  }
  else{
    printf("0324 NodeId: %d ngt->ngt recvGTTurnOff from %d(pid: %d) at %lf\n", myNodeId, ih->saddr(), ch->uid(),CURR_TIME); 
  }
	
  Packet::free(p);
}

bool
IDRM::base_lookup(nsaddr_t dst){
  //check if the baseAgent has an entry(valid) for dst	
	
  bool result = false;
	
  if (strcmp(baseRAgentName, "AODV") == 0){
    aodv_rt_entry *rt;
		
    rt = ((AODV *)baseRAgent)->rtable.rt_lookup( dst );
    if( rt && rt->rt_flags == RTF_UP) result = true;
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
    rtable_ent *prte;
    RoutingTable *rt = ((DSDV_Agent*)baseRAgent)->table_;
		
    prte = rt->GetEntry( dst ); //get an entry
    if( prte && prte->metric != 250 ) result = true;
  }	
	
  return result;
}

double
IDRM::base_hop_count(){
	
	
  double	hop_count =0.0;
  double	counter= 0.0;
	
  rtable_ent *prte;
  RoutingTable *table_ = ((DSDV_Agent*)baseRAgent)->table_;
	
  for (table_->InitLoop (); (prte = table_->NextLoop ());){
    if( prte && prte->metric != 250 ) {
      counter++;
      hop_count += prte->metric;
    }
  }
	
  double result = hop_count / counter;
  return result;
	
}

int
IDRM::dsdv_single_hops(){
	
  int counter =0;
	
  rtable_ent *prte;
  RoutingTable *table_ = ((DSDV_Agent*)baseRAgent)->table_;
	
  for (table_->InitLoop (); (prte = table_->NextLoop ());){
    if( prte && prte->metric == 1 ) {
      counter++;
    }
  }
	
  return counter;
}

//0316
int
IDRM::base_lookup(){
	
  int routes =0;
	
  if (strcmp(baseRAgentName, "AODV") == 0){
		
    aodv_rt_entry *rt;
    aodv_rtable rtable = ((AODV *)baseRAgent)->rtable;
    for(rt = rtable.head(); rt; rt = rt->rt_link.le_next) {
      if( rt && rt->rt_flags == RTF_UP && rt->rt_interface == 0) routes++;//only count valid, intra nodes
    }
  }
  else if (strcmp(baseRAgentName, "DSDV") == 0){
		
    rtable_ent *prte;
    RoutingTable *table_ = ((DSDV_Agent*)baseRAgent)->table_;
		
    for (table_->InitLoop (); (prte = table_->NextLoop ());){
      if( prte && prte->metric != 250 ) routes++;
			
    }
  }
	
  return routes;
}

bool
IDRM::intra_updated_entry_lookup(nsaddr_t dst){
  //return T: dst is updated by iIDRM
	
  for(int i=0; i<MAX_ROUTE; i++){
    if(intra_updated_entry[i] == dst) return true;
  }
  return false;
}


void
IDRM::stat_reset(){
  //reset stat
	
  n_out_beacon	=0;
  n_out_update	=0;
  n_out_table		=0;
  n_out_request =0;	
  n_out_data		=0;
	
  s_out_beacon	=0;
  s_out_update	=0;
  s_out_table		=0;
  s_out_request =0;	
  s_out_data		=0;
	
  n_out_intra_beacon	=0;
  n_out_intra_update	=0;
  n_out_intra_table	=0;
  n_out_intra_request =0; 
  n_out_intra_data		=0;
	
  s_out_intra_beacon	=0;
  s_out_intra_update	=0;
  s_out_intra_table	=0;
  s_out_intra_request =0; 
  s_out_intra_data		=0;
	
  n_in_beacon		=0;
  n_in_update		=0;
  n_in_table		=0;
  n_in_request =0;	
  n_in_data		=0;
	
  s_in_beacon		=0;
  s_in_update		=0;
  s_in_table		=0;
  s_in_request =0;	
  s_in_data		=0;
	
  n_in_intra_beacon	=0;
  n_in_intra_update	=0;
  n_in_intra_table	=0;
  n_in_intra_request =0;	
  n_in_intra_data		=0;
	
  s_in_intra_beacon	=0;
  s_in_intra_update	=0;
  s_in_intra_table	=0;
  s_in_intra_request =0;	
  s_in_intra_data		=0;
	
}


int
IDRM::getIndex(int nodeId){
  return nodeId % num_domain_members;
}

int
IDRM::getReverseIndex(int index){
  return myNodeId - getIndex(myNodeId) + index;
}


///////////////////////////////////////////////////////////
//
//				  CS 218 IDRM 2
//
///////////////////////////////////////////////////////////

//////////////////////////////////////
void
IDRM::adaptationRecalculateIntraBeaconInterval(void) {
	
  int old_connectivity = 1;
  int connectivity_diff = 0;
  double change_percentage;
  int minus = 0;
  int plus = 0;
  double oldIntraBeaconTimerSec;
	
  if (CURR_TIME - intra_last_update_ts < BEACON_INTERVAL_MAX)
    return;
	
  oldIntraBeaconTimerSec = intraBeaconTimerSec;
	
  for (int i=0; i< num_intra_gateway; i++) {
    if (intra_nodes_old[i])
      old_connectivity++;
    if (intra_nodes[i] != intra_nodes_old[i]) {
      connectivity_diff++;
      if(intra_nodes[i])
	plus++;
      else
	minus++;
    }
  }
	
  change_percentage = ceil( connectivity_diff * 100.0 / old_connectivity );
	
  if(change_percentage < 0)
    change_percentage = -change_percentage;
	
  if (change_percentage == 0) { // no change
		
    if ( intra_beacon_grace == BEACON_INTERVAL_RELAX_GRACE ) {
      intraBeaconTimerSec *= 2;
      intra_beacon_grace = 0;
    }
    else 
      intra_beacon_grace++;
		
  } else {
		
    intra_beacon_grace = 0;
		
    if ( change_percentage < 25	 ) { // low change
      intraBeaconTimerSec *= 0.9;
    } else if ( change_percentage < 75 ) { // high change
      intraBeaconTimerSec *= 0.75;
    } else {
      intraBeaconTimerSec *= 0.5;
    }
  }
	
  // ensure beacon timer is in [BEACON_INTERVAL_MIN, BEACON_INTERVAL_MAX]
  if ( intraBeaconTimerSec < BEACON_INTERVAL_MIN)
    intraBeaconTimerSec = BEACON_INTERVAL_MIN;
	
  if ( intraBeaconTimerSec > BEACON_INTERVAL_MAX)
    intraBeaconTimerSec = BEACON_INTERVAL_MAX;
	
  memset(intra_nodes_old, 0, sizeof(int) * num_intra_gateway);
  memcpy(intra_nodes_old, intra_nodes, sizeof(int) * num_intra_gateway);
  memset(intra_nodes, 0, sizeof(int) * num_intra_gateway);
	
  intra_last_update_ts = CURR_TIME;
	
  printf("[INTRA BEACON CHANGE] Time %.0lf ; NodeId %d ; Count %d +%d -%d ; Change: %.0lf \%; interval %.1lf => %.1lf\n",
	 CURR_TIME, myNodeId, old_connectivity, plus, minus, change_percentage, oldIntraBeaconTimerSec, intraBeaconTimerSec);
	
}

//////////////////////////////////////
void
IDRM::adaptationRecalculateBeaconInterval(void) {
	
  int old_connectivity = 0; // count myself
  int connectivity_diff = 0;
  double change_percentage;
  int minus = 0;
  int plus = 0;
  double oldBeaconTimerSec;	
	
  if (CURR_TIME - inter_last_update_ts < BEACON_INTERVAL_MAX) //perform topology change check
    return;
	
  oldBeaconTimerSec = beaconTimerSec;
	
  for (int i=0; i< NUM_DST; i++) {
    if (inter_nodes_old[i])
      old_connectivity++;
    if (inter_nodes[i] != inter_nodes_old[i]) {
      connectivity_diff++;
      if(inter_nodes[i])
	plus++;
      else
	minus++;
    }
  }
	
  change_percentage = ceil( connectivity_diff * 100.0 / (old_connectivity ? old_connectivity : 1) );
	
  if(change_percentage < 0)
    change_percentage = -1 * change_percentage;
	
  if (change_percentage == 0) { // no change
		
    if ( beacon_grace == BEACON_INTERVAL_RELAX_GRACE ) {
      beaconTimerSec *= 2;
      beacon_grace = 0;
    }
    else 
      beacon_grace++;
		
  } else {
		
    beacon_grace = 0;
		
    if ( change_percentage < 25	 ) { // low change
      beaconTimerSec *= 0.9;
    } else if ( change_percentage < 75 ) { // high change
      beaconTimerSec *= 0.75;
    } else {
      beaconTimerSec *= 0.5;
    }
  }
	
  // ensure beacon timer is in [BEACON_INTERVAL_MIN, BEACON_INTERVAL_MAX]
  if ( beaconTimerSec < BEACON_INTERVAL_MIN)
    beaconTimerSec = BEACON_INTERVAL_MIN;
	
  if ( beaconTimerSec > BEACON_INTERVAL_MAX)
    beaconTimerSec = BEACON_INTERVAL_MAX;
	
  memset(inter_nodes_old, 0, sizeof(int) * NUM_DST);
  memcpy(inter_nodes_old, inter_nodes, sizeof(int) * NUM_DST);
  memset(inter_nodes, 0, sizeof(int) * NUM_DST);
	
  inter_last_update_ts = CURR_TIME;
	
  printf("[INTER BEACON CHANGE] Time %.0lf ; NodeId %d ; Count %d +%d -%d ; Change: %.0lf \% ; interval %.1lf => %.1lf\n",
	 CURR_TIME, myNodeId, old_connectivity, plus, minus, change_percentage, oldBeaconTimerSec, beaconTimerSec);
	
}

//////////////////////////////////////////////////
void
IDRM::adaptationDynamicGatewayElection()
{
  //cs218_idrm2 dynamic election
	
  int index = getIndex(myNodeId);
	
  // HACK to make sure the lonely guy (no intra neighbors) gets elected
  int num_of_intra_neighbors = 0;
	
  int c_intra_n =0;
  for(int i=0; i<num_intra_gateway; i++){
    if(my_reachables[myNodeId-index+i]	== 1){
      c_intra_n++;
    }
	}

  if(c_intra_n > 1){	
    int* the_sorted_gateways = new int[num_intra_gateway];
    memset(the_sorted_gateways, -1, sizeof(int)*num_intra_gateway);
				
    findBestGateways(the_sorted_gateways, 0);
    now_gateway = 0;
    for (int i = 0; i < num_intra_gateway; i++) {
    	
    	if(the_sorted_gateways[i] >= 0){
    			//printf("0214SH GD_IN NodeID: %d inc: %d at %lf\n", myNodeId,the_sorted_gateways[i], CURR_TIME );

    		 	global_decision[the_sorted_gateways[i]] ++; //0214 sh
    	}

      if (the_sorted_gateways[i] == myNodeId) {
				now_gateway = 1;
      }
    }
  }
  else {
		
    now_gateway = 1;
		
  }
	
  if(IDRM_DEBUG)
    printf("\n");
	
  if(now_gateway){ //let DSDV know..
    if(strcmp(baseRAgentName, "DSDV") == 0){
      ((DSDV_Agent *)baseRAgent)->my_gateway_list[index] = 1;
    }
  }
  else{ //let DSDV and AODV know..
		
    if(strcmp(baseRAgentName, "DSDV") == 0){
      ((DSDV_Agent *)baseRAgent)->my_gateway_list[index] = 0;
    }
    else if(strcmp(baseRAgentName, "AODV") == 0){
      for(int i=0; i< n_idrm_rt_entry; i++){
	aodv_rt_entry *rt;
	rt = ((AODV *)baseRAgent)->rtable.rt_lookup( idrm_rt_table[i].dst );
	if(rt)
	  ((AODV *)baseRAgent)->nb_delete(idrm_rt_table[i].dst);
				
      }
    }
  }
	
  //cs218_idrm2 clear my reachable and others reachable
  //memset(my_reachables, 0, sizeof(bool)*NUM_DST);
  //memset(others_reachables, 0, sizeof(bool)*NUM_DST*num_intra_gateway);
  //memset(excluded_reachables, 0, sizeof(bool)*NUM_DST);
  //
	
}

void 
IDRM::findBestGateways(int* the_sorted_gateways, int index) {
	
  int temp_max_dst = -1;
  int temp_max_node = -1;
  int count = 0;
  int count_global = 0;
  int offset = 0;
  int real_offset = 0;
  //find max dst gateway
  printf("findBest nodeId %d, time %lf, run#%d\n", myNodeId, CURR_TIME, index);
  if(IDRM_DEBUG) {
    for (int i = 0; i < num_intra_gateway*NUM_DST; i++) {
      printf("%d,", others_reachables[i]);
    }
    printf("\n");
  }
  for (int i = 0; i < num_intra_gateway; i++) {
    offset = i*NUM_DST;
    real_offset = (myNodeId-getIndex(myNodeId)+i)*NUM_DST;
    count = 0;
    count_global=0;
    if (others_reachables[offset] == -2) {
      continue; //this is a chosen gateway, skip
    }
    for (int j = 0; j < NUM_DST; j++) {
      if (others_reachables[offset+j] > 0) {
	count++;
      }

    }

		if (count > temp_max_dst) {
      temp_max_dst = count;
      temp_max_node = i;
    }
  }
  printf("\n");
  //at this point, max_node is found, mark invalid flag//
  offset = temp_max_node*NUM_DST;
  if (IDRM_DEBUG) {
    //cout<<"max dst:"<<temp_max_dst<<"; max nodeId:"<<temp_max_node<<endl;
    printf("Printing1 Count at %d %d %d %d at %lf decisionID,CT: %d %d Neighbors: ", myNodeId, count, count_global, real_offset, CURR_TIME, temp_max_dst, temp_max_node);
    printf("\n");
  }
  if(IDRM_DEBUG) {
    for (int i = 0; i<NUM_DST; i++) {
      cout<<others_reachables[offset+i]<<";";
    }
    printf("\n");
  }
  for (int i = 0; i < NUM_DST; i++) {
    if (others_reachables[offset+i] > 0) {
      for (int j = 0; j < num_intra_gateway; j++) {
	others_reachables[j*NUM_DST+i] = -1; //adjust reachable dst for everyone (including self) 
      }
    }
  }
  others_reachables[offset] = -2; //mark skip flag
  the_sorted_gateways[index] = getReverseIndex(temp_max_node); //put node into array
	  
  //global_decision[getReverseIndex(temp_max_node)] = 1; //0214 sh
  //
	
  index++;
  //check if all dsts are covered
  bool finished = 1;
  for (int i = 0; i < num_intra_gateway*NUM_DST; i++) {
    if (others_reachables[i] > 0) {
      finished = 0;
      break;
    }
  }
  if (!finished) {
    findBestGateways(the_sorted_gateways, index);
  }
  else {
    return;
  }
  
}

#include <iostream>
#include <assert.h>
#include <unistd.h>
#include <ctime>
#include <chrono>
#include <mutex>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>

using namespace std;

#define type_ first
#define packet_ second
#define none (int)(-1)
const int N = 16, INF = 0x3f3f3f3f, number_thread = N;

bool flag_controller = true;

int controller_index;

int unq = 100;

unordered_map<string, int> encode = {{"display flow", 1}, {"exit", 3}, {"display idx", 4}};
unordered_map<int, string> decode = {{1, "display flow"}, {0, "ERRO: date is erro\n"}, {3, "exit"}, {4, "display idx"}};

int ping_exec;
int tcp_exec;

struct Packet{               //hello request packet: {source, INF, 0, 0, CONTROLLER_INDEX}
	int source, dest;    //hello reply packet: {source, dest, 8, 0, 0}
    			     //ping request packet: {source, dest, 1, 1, 0}
	int type, code;      //ping reply packet: {source, dest, 1, 2,0}
        int content;	     //ping out of reach: {source, dest, 1, 3, 0}
			     //tcp syn: {source, dest, 2, 3, SEQ1/0}
			     //tcp syn/ack: {source, dest, 2, 4, SEQ1/SEQ2}
			     //tcp ack: {source, dest, 2, 5, SEQ2} SEQ2?
			     //tcp fin: {source, dest 2, 6, 0}
			     //tcp date: {source, dest, 3, 1}
};

int seq1, seq2;

bool flag_tcp;

static bool f[N][N];                // f[x][y] == 1 is pure on one link   x -> y
queue<Packet> g[N][N];      //Equipment diagram -> index is equ, space is date packet

struct Flow_tables{
        int source, dest;
       	int type;	            // type = 0 is direct; type = 1 is flow;
	int actions;     // 000 is drop, xxx is out_port
	
	bool operator == (const Flow_tables &t) const{
		return  t.source == source && \
			t.dest == dest && \
			t.type == type && \
			t.actions == actions;
	}
};

struct flow_tables_hash{
	size_t operator () (const Flow_tables &t) const{
		return t.source*26*26*26 + t.dest*26*26 + t.type*26 + t.actions;
	}
};

struct Equ{
	int idx;
	string ip_address;
	int status;          //4 Bytes == 32 bit 
        unordered_set<Flow_tables, flow_tables_hash>  flow_tables;
};
Equ equ[N];

mutex mtx[N][N];
mutex mtx_f[N][N];
queue<Packet> buff_queue[N][2]; // buff[1] is broadcoast; buff[1] is unicoast;
mutex mtx_buff_queue[N][2];
mutex mtx_flow[N];
mutex mtx_ping;
mutex mtx_tcp;

mutex mtx_tcp_date[N];
mutex mtx_tcp_date_buff[N];

mutex controller_send_mtx;
queue<Packet> controller_send_buff;
queue<Packet> tcp_date_buff[N];

// ---------------------------------------------------------------------------

void f_(int x, int y){
	f[x][y] = 1; f[y][x] = 1;
}

void create_diagram(){
	f_(0,1);
	f_(0,2);
	f_(1,3);
	f_(1,4);
	f_(1,5);
	f_(2,6);
	f_(2,7);
	f_(3,8);
	f_(3,9);
	f_(4,10);
	f_(4,11);
	f_(6,12);
	f_(6,13);
	f_(7,14);
	f_(7,15);
	for(int i = 16 ; i < N ; i ++)
		f_(7, i);
}

void init_flow_tables(){
	for(int i = 0 ; i < N ; i ++)
		for(int j = 0 ; j < N ; j ++)
		{
			mtx_f[i][j].lock();
			if(!f[i][j]) {
				mtx_f[i][j].unlock();
				continue;
			}
			mtx_f[i][j].unlock();

			equ[i].flow_tables.insert({i, j, 0, j});   // i -> j is direct and out_port is j;
		}
}

void init_idx(){
	for(int i = 0 ; i < N ; i ++) equ[i].idx = i;
}

void init(){
	controller_index = 0;	 			// controller init: {CONTROLLER_INDEX, 1, NULL}  
							// controller_index must lower than 15
	equ[controller_index].idx = controller_index;
	equ[controller_index].status = (controller_index << 1) + 1; // xxx1

	create_diagram();

	init_flow_tables();

	init_idx();
}

void time30hello_request(){
	int k = 0;
	while(flag_controller)
	{
		for(int i = 0 ; i < N ; i ++)
		{
			if(!flag_controller){
				cout << controller_index << " request is return " << endl;
				return;
			}
			mtx_f[controller_index][i].lock();
			if(!f[controller_index][i]) {
				mtx_f[controller_index][i].unlock();
				continue;
			}
			mtx_f[controller_index][i].unlock();

			mtx[controller_index][i].lock();
			g[controller_index][i].push({controller_index, INF, 0, 0, (controller_index  << 1) + 1});
			mtx[controller_index][i].unlock();
		}

		this_thread::sleep_for(chrono::milliseconds(30000));    // sleep time is 30s
	/*	
		mtx_flow[controller_index].lock();

		equ[controller_index].flow_tables.clear();
		for(int i = 0 ; i < N ; i ++)
		{
			if(i == controller_index || !f[controller_index][i]) continue;
			equ[controller_index].flow_tables.insert({0, i, 0, i});
		}

		mtx_flow[controller_index].unlock();
		cout << "flow is clear" << endl;
	*/
	}
	cout << controller_index << " request is return" << endl;

}

void hello_reply_packet_modify(Packet &packet, int equ_index, int in_port){
	mtx_flow[controller_index].lock();
	equ[controller_index].flow_tables.insert({controller_index, packet.source, 1, in_port});
	mtx_flow[controller_index].unlock();
}

void time10hello_reply(){
	while(flag_controller)
	{
		for(int i = 0 ; i < N ; i ++)
		{
			if(!flag_controller) {
				cout << controller_index << " reply is return " << endl;
				return;
			}

			mtx_f[i][controller_index].lock();
			if(!f[i][controller_index]) {
				mtx_f[i][controller_index].unlock();
				continue;
			}
			mtx_f[i][controller_index].unlock();
			
			mtx[i][controller_index].lock();
			while(!g[i][controller_index].empty())
			{
				Packet packet_copy = g[i][controller_index].front();
				g[i][controller_index].pop();

				if(packet_copy.type == 8 && packet_copy.code == 0) hello_reply_packet_modify(packet_copy, controller_index, i);
				else if((packet_copy.type == 1 && packet_copy.code == 2) ||\
					 (packet_copy.type == 1 && packet_copy.code == 3)) {
					cout << packet_copy.source  << " -> " << packet_copy.dest << "   ";
					ping_exec = packet_copy.source;
				       	mtx_ping.lock();
				}
				else if(packet_copy.type == 2 && packet_copy.code == 4){
					seq1 = packet_copy.content >> 16;
					seq2 = packet_copy.content & 65535;

					cout << "controller: " << seq1 << "  -> " << packet_copy.source << ": " << seq2 << \
						"is scuess" << endl;

					controller_send_mtx.lock();
					controller_send_buff.push({controller_index, packet_copy.source, 2, 5, packet_copy.content & 65535});
					controller_send_mtx.unlock();

					tcp_exec = packet_copy.source;
					mtx_tcp.lock();
				}
				else if(packet_copy.type == 3 && packet_copy.code == 2){
					cout << decode[packet_copy.content];
					mtx_tcp.lock();
				}
				else if(packet_copy.type == 2 && packet_copy.code == 6){
					controller_send_mtx.lock();
					controller_send_buff.push({controller_index, packet_copy.source, 2, 5, packet_copy.content & 65535});
					controller_send_mtx.unlock();

					tcp_exec = packet_copy.source;
					mtx_tcp.lock();

				}
			}
			mtx[i][controller_index].unlock();
		}

		this_thread::sleep_for(chrono::milliseconds(500));    // sleep time is 0.5s		
	}
	cout << controller_index << " reply is return" << endl;
}

void exit_(){
	flag_controller = false;
	return;
}

void display_state_(){
	system("clear");
	for(int i = 0 ; i < N ; i ++)
	{
		for(int j = 0 ; j < N ; j ++)
		{
			mtx[i][j].lock();
			cout << g[i][j].size() << " ";
			mtx[i][j].unlock();
		}
		cout << endl;
	}
}

void display_flow_tables_(){
	system("clear");
	cout << "Source  ->  Dest  Type  actions" << endl;
	for(auto x : equ[controller_index].flow_tables)
	{
		cout << x.source << "#      ->    " << x.dest << "#  " << x.type << "   " << x.actions << "    " << endl;
	}
}

void erro_(){
	cout << "erro: invalid instruction" << endl;
}

void division_str(string cin_, string exec[]){
	string temp = "";
	
	int num = 0;
	for(int i = 0 ; i < cin_.size() ; i ++)
	{
		if(cin_[i] < 32 || (cin_[i] > 32 && cin_[i] < 48) || (cin_[i] > 57 && cin_[i] < 65) || (cin_[i] > 90 && cin_[i] < 97) || cin_[i] > 122){
		       	exec[0] = "erro";
			break;
		}

		if(cin_[i] == ' ' && temp == "") continue;

		if(cin_[i] == ' '){
		       	exec[num ++] = temp;
			temp = "";
		}
		
		if(cin_[i] != ' ') temp += cin_[i];
	}

	if(num > 5) exec[0] = "erro";

	if(temp != "") exec[num ++] = temp;

	temp = "";
}

int controller_check_flow_tables(int dest){
	int out_port = -1;

	mtx_flow[controller_index].lock();
	for(auto x : equ[controller_index].flow_tables)
		if(x.dest == dest) out_port = x.actions;
	mtx_flow[controller_index].unlock();

	return out_port;	
}

int send_ping(string str){
	int dest = stoi(str);

	int out_port = controller_check_flow_tables(dest);
	
	if(out_port == -1) {
		return -1;
	}

	mtx[controller_index][out_port].lock();
	g[controller_index][out_port].push({controller_index, dest, 1, 1, 0});
	mtx[controller_index][out_port].unlock();

	return out_port;
}

bool wait_(mutex &mtx_){
	while(mtx_.try_lock())
	{
		mtx_.unlock();
		this_thread::sleep_for(chrono::milliseconds(500)); 
	}

	 mtx_.unlock();
	 
	return true;
}

int send_tcp(string str){
	int dest = stoi(str);

	int out_port = controller_check_flow_tables(dest);
	if(out_port == -1){
		return -1;
	}

	seq1 = rand() % (32767);
	mtx[controller_index][out_port].lock();
	g[controller_index][out_port].push({controller_index, dest, 2, 3, seq1 << 16});
	mtx[controller_index][out_port].unlock();

	return out_port;
}

void shell_controller(){

	string cin_;
	string exec[5] = {""};
	while(true)
	{
		cout << "\033[1m\033[33m\033[7mController# >\033[0m";
		getline(cin, cin_);
		if(cin_ == "\n") continue;

		division_str(cin_, exec);

		if(exec[0] == "exit") {
			exit_();
			return ;
		}
		else if(exec[0] == "display"){
			if(exec[1] == "state")
				display_state_();
			else if(exec[1] == "flow")
				display_flow_tables_();
			else erro_();
		}
		else if(exec[0] == "ping"){
			int k = 0;
			while(k ++ < 5)
			{
				controller_send_mtx.lock();
				controller_send_buff.push({controller_index, stoi(exec[1]), 1, 1, 0});
				controller_send_mtx.unlock();

				bool flag = wait_(mtx_ping);
				if(flag && (ping_exec == stoi(exec[1]))) cout << "PING " << exec[1] << "#:  success" << endl;
				else {
					ping_exec = 0;;
				}
			}
		}
		else if(exec[0] == "tcp"){
			controller_send_mtx.lock();
			controller_send_buff.push({controller_index, stoi(exec[1]), 2, 3, 0});
			controller_send_mtx.unlock();
			
			bool flag = wait_(mtx_tcp);
			if(flag && (tcp_exec == stoi(exec[1]))) 
			{
				cout << "TCP seesion:    O#  ->   " << exec[1] << "#  is success." << endl;
				tcp_exec = 0;

				controller_send_mtx.lock();
				controller_send_buff.push({controller_index, stoi(exec[1]), 2, 6, 0});
				controller_send_mtx.unlock();

				flag = wait_(mtx_tcp);
				if(flag && (tcp_exec == stoi(exec[1]))) cout << "TCP seesion:    O#  !=>   " << exec[1] << "#  is success." << endl;
				tcp_exec = 0;
			}
		}
		else if(exec[0] == "ssh"){
			controller_send_mtx.lock();
			controller_send_buff.push({controller_index, stoi(exec[1]), 2, 3, 0});
			controller_send_mtx.unlock();
			
			bool flag = wait_(mtx_tcp);
			if(flag && (tcp_exec == stoi(exec[1]))) {
				cout << "TCP seesion:    O#  ->   " << exec[1] << "#  is success." << endl;
				
				cout << "\033[1m\033[33m\033[7m" << exec[1] << "#:   >\033[0m";
				string str_ssh = "";
				while(str_ssh != "exit")
				{
					getline(cin, str_ssh);

					controller_send_mtx.lock();
					if(str_ssh == "exit") controller_send_buff.push({controller_index, stoi(exec[1]), 2, 6, 0});
					else controller_send_buff.push({controller_index, stoi(exec[1]), 3, 1, encode[str_ssh]});

					controller_send_mtx.unlock();
					
					bool flag = wait_(mtx_tcp);
					if(flag && (tcp_exec == stoi(exec[1]))){
						
					}
					if(str_ssh != "exit") cout << "\033[1m\033[33m\033[7m" << exec[1] << "#:   >\033[0m";
				}
			}
			tcp_exec = 0;
		}
		else {
			erro_();
		}	
	}
}

void send_controller(){
	queue<Packet> buff;

	while(flag_controller)
	{
		controller_send_mtx.lock();
		if(!controller_send_buff.empty() && buff.empty()) buff.swap(controller_send_buff);
		controller_send_mtx.unlock();

		while(!buff.empty() && flag_controller)
		{
			auto tmp_ = buff.front();
			buff.pop();

			int out_port = controller_check_flow_tables(tmp_.dest);
			if(out_port == -1) {
				if(tmp_.type == 1 && tmp_.code == 1){
					ping_exec = 0;
					mtx_ping.lock();
				}
				else if(tmp_.type == 2 && tmp_.code == 3){
					tcp_exec = 0;
					mtx_tcp.lock();
				}
				cout << "Dest  is  out  of  search." << endl;
				continue;
			}
			
			if(tmp_.type == 2 && tmp_.code == 3) tmp_ = {tmp_.source, tmp_.dest, tmp_.type, tmp_.code, (rand() % (32767) << 16)};
			else if(tmp_.type == 2 && tmp_.code == 5) tmp_ = {tmp_.source, tmp_.dest, tmp_.type, tmp_.code, seq2};
			mtx[controller_index][out_port].lock();
			g[controller_index][out_port].push(tmp_);
			mtx[controller_index][out_port].unlock();
		}
		sleep(1);
	}	
	
}

void controller_work(int controller_index){
	
	thread hello_request30(time30hello_request);

	thread shell_controllers(shell_controller);
	thread send_controllers(send_controller);
	thread hello_reply10(time10hello_reply);

	hello_request30.join();
	shell_controllers.join();
	send_controllers.join();
	hello_reply10.join();
}

//-------------------------equ_work-------------------------------------------------------------


void send_boadcoast_packet(int equ_index, queue<Packet> &buff_boadcoast);
void send_unicoast_packet(int equ_index, queue<Packet> &buff_unicoast);

void send_packet(int equ_index){
	queue<Packet> buff_boadcoast;
	queue<Packet> buff_unicoast;

	while((buff_boadcoast.empty() || buff_unicoast.empty()) && (flag_controller))
	{
		if(!flag_controller) {
			cout << equ_index << " send_packet is return " << endl;
			return;
		}

		if(buff_boadcoast.empty() && mtx_buff_queue[equ_index][0].try_lock())
		{
			buff_boadcoast.swap(buff_queue[equ_index][0]);
			mtx_buff_queue[equ_index][0].unlock();
			if(!buff_boadcoast.empty()) send_boadcoast_packet(equ_index, buff_boadcoast);
		}

		if(buff_unicoast.empty() && mtx_buff_queue[equ_index][1].try_lock())
		{
			buff_unicoast.swap(buff_queue[equ_index][1]);
			mtx_buff_queue[equ_index][1].unlock();
			if(!buff_unicoast.empty()) send_unicoast_packet(equ_index, buff_unicoast);
		}
		sleep(1);
	}
	cout << equ_index << " send_packet is return" << endl;
}

pair<bool, int> check_flow_tables(int equ_index, Packet tmp_){
	pair<bool, int> flag = {false, 0};

	mtx_flow[equ_index].lock();
	for(auto x : equ[equ_index].flow_tables)
	{
		if(x.dest == tmp_.dest) flag = {true, x.actions};
	}
	mtx_flow[equ_index].unlock();

	return flag;
}

int check_flow_tables(int equ_index, int dest){
	int out_port = -1;

	mtx_flow[equ_index].lock();
	for(auto x : equ[equ_index].flow_tables)
	{
		if(x.dest == dest) out_port = x.actions;
	}
	mtx_flow[equ_index].unlock();

	return out_port;
}

void send_unicoast_packet(int equ_index, queue<Packet> &buff_unicoast){
	while(!buff_unicoast.empty())
	{
		auto tmp_ = buff_unicoast.front();
		buff_unicoast.pop();

		auto flag = check_flow_tables(equ_index, tmp_);
		if(flag.first){
			mtx[equ_index][flag.second].lock();
			g[equ_index][flag.second].push(tmp_);
			mtx[equ_index][flag.second].unlock();
		}
		else if(!flag.first && tmp_.type == 1 && tmp_.code == 1){
			tmp_ = {equ_index, tmp_.source, 1, 3, 0};
			auto flag_tmp = check_flow_tables(equ_index, tmp_); //type, code == 1, 3 is meaning out of reach
		
			mtx[equ_index][flag.second].lock();
			g[equ_index][flag.second].push(tmp_);
			mtx[equ_index][flag.second].unlock();
		}
	}
}

void send_boadcoast_packet(int equ_index, queue<Packet> &buff_boadcoast){
	while(!buff_boadcoast.empty())
	{
		auto tmp_ = buff_boadcoast.front();
		buff_boadcoast.pop();

		for(int i = 0 ; i < N ; i ++)
		{
			mtx_f[equ_index][i].lock();
			if(!f[equ_index][i] || i == tmp_.source){
				mtx_f[equ_index][i].unlock();	
				continue;
			}
			mtx_f[equ_index][i].unlock();

			tmp_.source = equ_index;                      // modity hello request's source of equ_index;
			mtx[equ_index][i].lock();
			g[equ_index][i].push(tmp_);
			mtx[equ_index][i].unlock();
		}
	}
}

void add_flow_table(Flow_tables packet, int equ_index){
	mtx_flow[equ_index].lock();
	equ[equ_index].flow_tables.insert(packet);
	mtx_flow[equ_index].unlock();
}

void equ_status_modity(Packet packet, int equ_index, int in_port){
	if(packet.type == 0 && packet.code == 0){                          // hello_rquest is 0 and 0
	       	equ[equ_index].status |= packet.content;

		Flow_tables tmp_ = {equ_index, (packet.content & 30) >> 1, 1, in_port};
		add_flow_table(tmp_, equ_index);
	}
	else if(packet.type == 8 && packet.code == 0){                     // hello_reply is 8 and 0
		Flow_tables tmp_ = {equ_index, packet.source, 1, in_port};
		add_flow_table(tmp_, equ_index);
	}
}

Packet reply_packet(string exec, int equ_index, int dest, int source){
	if(exec == "display flow") {
		string tmp = "Source  ->  Dest  Type  actions \n";
	       	for(auto x : equ[equ_index].flow_tables)
	       		tmp += to_string(x.source) + "#      ->    " + to_string(x.dest) + "#  " + to_string(x.type) + "   " + to_string(x.actions) + "     \n";
		
		decode[unq ++] = tmp;

	        return {source, dest, 3, 2, unq - 1};
	}
	else if(exec == "display idx"){
		string tmp = "This is " + to_string(equ[equ_index].idx) + "#" + '\n';
		decode[unq ++] = tmp;

	        return {source, dest, 3, 2, unq - 1};

	}
	return {source, dest, 3, 2, 0};
}

void tcp_date(int equ_index){
	queue<Packet> buff;
	while(flag_controller)
	{
		if(flag_controller && mtx_tcp_date[equ_index].try_lock()) {
			mtx_tcp_date[equ_index].unlock();
			flag_tcp = true;
			return;
		}
		if(!tcp_date_buff[equ_index].empty() && flag_controller && mtx_tcp_date_buff[equ_index].try_lock())
		{
			if(buff.empty()) buff.swap(tcp_date_buff[equ_index]);
			mtx_tcp_date_buff[equ_index].unlock();

			if(mtx_tcp_date[equ_index].try_lock()){
				mtx_tcp_date[equ_index].unlock();
				flag_tcp = true;
				return;
			}
		}

		while(!buff.empty() && flag_controller)
		{
			auto tmp_ = buff.front();
			buff.pop();

			string exec = decode[tmp_.content];

			tmp_ = reply_packet(exec, equ_index, tmp_.source, tmp_.dest);
			
			int out_port = check_flow_tables(equ_index, tmp_.dest);	

			mtx[equ_index][out_port].lock();
			g[equ_index][out_port].push(tmp_);
			mtx[equ_index][out_port].unlock();
			
			if(mtx_tcp_date[equ_index].try_lock()){
				mtx_tcp_date[equ_index].unlock();
				flag_tcp = true;
				return;
			}
		}
		sleep(1);
	}


}

void process_packet(int equ_index){
	queue<Packet> buff;
	queue<Packet> buff_boadcoast;
	queue<Packet> buff_unicoast;

	while(flag_controller)
	{
		for(int i = 0 ; i < N ; i ++)
		{

			if(!flag_controller) 
			{
				cout << equ_index << "process_packet is return " << endl;
				return;
			}

			mtx_f[i][equ_index].lock();
			if(!f[i][equ_index]) {
				mtx_f[i][equ_index].unlock();
			       	continue;
			}
			mtx_f[i][equ_index].unlock();

			mtx[i][equ_index].lock();
			if(g[i][equ_index].empty()){
				mtx[i][equ_index].unlock();       
				continue;
			}
			buff.swap(g[i][equ_index]);                  // avoid equ rival g[i][equ_index]
			mtx[i][equ_index].unlock();
			
			while(!buff.empty() && flag_controller)
			{
				auto tmp_ = buff.front();
				buff.pop();

				equ_status_modity(tmp_, equ_index, i);  // type, code is 0,0 and 8,0 can modity status
				
				if(tmp_.dest == INF){
				       	buff_boadcoast.push(tmp_);
					tmp_ = {equ_index, (tmp_.content & 30) >> 1, 8, 0, equ[equ_index].status};
					buff_unicoast.push(tmp_);
				}
				else if(tmp_.dest != equ_index) buff_unicoast.push(tmp_);
				else if(tmp_.dest == equ_index){
					if(tmp_.type == 1 && tmp_.code == 1) //ping request
						buff_unicoast.push({equ_index, controller_index, 1, 2, 0});
					else if(tmp_.type == 2 && tmp_.code == 3)
					{
						buff_unicoast.push({equ_index, controller_index, 2, 4, tmp_.content + (rand() % (32767))});
						mtx_tcp_date[equ_index].lock();	
						thread tcp_dates(tcp_date, equ_index);
						tcp_dates.detach();
					}
					else if(tmp_.type == 2 && tmp_.code == 6)  // tcp fin
					{
						buff_unicoast.push({equ_index, controller_index, 2, 5, seq1});
						mtx_tcp_date[equ_index].unlock();
						while(!flag_tcp) sleep(1);
						buff_unicoast.push({equ_index, controller_index, 2, 6, seq2});
					}
					else if(tmp_.type == 3 && tmp_.code == 1)
					{
						mtx_tcp_date_buff[equ_index].lock();
						tcp_date_buff[equ_index].push(tmp_);
						mtx_tcp_date_buff[equ_index].unlock();
					}
				}
			}

			while((!buff_boadcoast.empty() || !buff_unicoast.empty()) && flag_controller) 
			{
				if(!buff_boadcoast.empty() && mtx_buff_queue[equ_index][0].try_lock())
				{
					buff_boadcoast.swap(buff_queue[equ_index][0]);
					mtx_buff_queue[equ_index][0].unlock();
				}
				if(!buff_unicoast.empty() && mtx_buff_queue[equ_index][1].try_lock())
				{
					buff_unicoast.swap(buff_queue[equ_index][1]);
					mtx_buff_queue[equ_index][1].unlock();
				}
			}
		}
		sleep(1);
	}
	cout << equ_index << " process_packet is return" << endl;
}

void equ_work(int equ_index) {
	thread process_packets(process_packet, equ_index);
	
	thread send_packet1(send_packet, equ_index);
	
	process_packets.join();
	send_packet1.join();
}

clock_t start_, end_;
bool clock_flag = false;

int main(){
	init();
	system("clear");
	system("date");
	
	vector<thread> equs_thread;

	for(int i = 1 ; i < N ; i ++)
		equs_thread.push_back(thread(equ_work, i));
	equs_thread.push_back(thread(controller_work, controller_index));
	
	for(int i = 0 ; i < N ; i ++)
		equs_thread[i].join();

	return 0;
}

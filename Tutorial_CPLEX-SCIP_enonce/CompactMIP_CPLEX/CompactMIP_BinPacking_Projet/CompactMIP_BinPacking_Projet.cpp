#include <ilcplex/ilocplex.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <algorithm>

#include"../../Graph/Graph.h"
#include "./VRPfileParser.h"
#include "./solve_relaxedPLNE.h"
#include "./mtz_plne.cpp"
//mtz_plne.cpp contains the basic mtz without reinforcing
#include "./VRPtools.h"
//  string vectorint_tostring(vector<int> my_vector);
//  void write_solution_to_file(vector<vector<int>> tournees, C_Graph* G, string filename);
//  void print_all_tournees(vector<vector<int>> tournees, C_Graph* G);
#include "VRPheuristique.h"
//void optimize_2opt_internalRoutes(vector<vector<int>> *tournees, C_Graph* G);
//void optimize_2opt_switchRoutes(vector<vector<int>> *tournees, C_Graph* G);

#define CPX_PARAM_TILIM 300

#define epsilon 0.00001

using namespace std;



//Coupe Min inequality separation algorithm
void  find_ViolatedCoupeMinCst(IloEnv env, C_Graph* G,  vector<vector<IloNumVar>>& x, vector<vector<float>>&fracsol, list<IloRange> & L_ViolatedCst){

  // arrondis toutes les valeurs puis on fait une recherche de coupe min
  vector<vector<int>> intsol;
  intsol.resize(G->nb_nodes);
  for(int i=0;i<G->nb_nodes;i++)
    intsol[i].resize(G->nb_nodes);
      

  for(int i = 0; i < G->nb_nodes; i++) {
    for(int j = 0; j < G->nb_nodes; j++) {
        intsol[i][j] = 0; //init val
        if(fracsol[i][j] >= 0.5){ //avec un seuil 0.5 = arrondis
            intsol[i][j] = 1;
          //  intsol[j][i] = 1; //not needed for undirected graph
         }
      
     // cout << fracsol[i][j] << "   " ;
    }
  }

  C_Graph* undirected_graph;
  undirected_graph = intsol_to_undirected_C_Graph(intsol, G);

  //pas besoin d'enlever le sommet 0 car chaque arc à son inverse et on coupe en mode non-orienté
  // ça revient donc au même (si le sommet 0 est dans pas S il est dans non_S)

  list<int> listcut;
  double mincutval = undirected_graph->Undirected_MinimumCut(listcut); 

  vector<int> vectorcut;
  vectorcut.insert(vectorcut.begin(), listcut.begin(),listcut.end()); //peut-être que ca tue la liste ?

  //TODO VOIR SI CEST <2 OU <1 VU QUE LA FONCTION DE COUPE EST UNDIRECTED
  if(mincutval < 2 && vectorcut.size() >= 2) { // si contrainte violée et au moins 2 sommets dedans
    //on l'ajoute
    
    //cout << "valeur de la coupe min violant contrainte : " << mincutval << " de "<< vectorcut.size()<<" sommets"<< endl;

    IloExpr expr(env);
    for(int i=0; i<G->nb_nodes;i++){
      for(int j=0; j<G->nb_nodes;j++){
        //if(i is in listcut && j is not in listcut)
        if(i < j && vector_contains(vectorcut, i) && ! vector_contains(vectorcut, j)
              ||vector_contains(vectorcut, j) && ! vector_contains(vectorcut, i))
        {
          expr+=x[i][j];
        }
      }
    }
    //cout << expr << endl;

    IloRange newCte = IloRange(expr >= 1);
    //cout << newCte << endl;
    L_ViolatedCst.push_back(newCte);

    // on peut aussi formuler la contrainte inverse: de NON_coupe vers coupe on veut aussi un arc sortant
    IloExpr expr2(env);
    for(int i=0; i<G->nb_nodes;i++){
      for(int j=0; j<G->nb_nodes;j++){
        if(i < j && ! vector_contains(vectorcut, i) && vector_contains(vectorcut, j)
              ||vector_contains(vectorcut, j) && ! vector_contains(vectorcut, i))
        {
          expr2+=x[i][j];
        }
      }
    }
    //cout << expr2 << endl;

    IloRange newCte2 = IloRange(expr2 >= 1);
    //cout << newCte << endl;
    L_ViolatedCst.push_back(newCte2);
   // cout << "adding usercut constraint with mincut" << endl;
  }
}


//Capacity inequality separation algorithm
void  find_ViolatedCapacityCst(IloEnv env, C_Graph* G,  vector<vector<IloNumVar>>& x, vector<vector<int>>&intsol, list<IloRange> & L_ViolatedCst){
  vector<vector<int>> tournees;
  //copied from optimize_undirected()

  //on va aussi exporter la solution dans le pointeur d'entrée "tournees"
  //cout << "\n edges in undirected graph solution : " << endl;
  vector<pair<int,int>> sol; // marche pour tous
  for(int i=0; i < G->nb_nodes; i++) {
    for (int j=0; j < G->nb_nodes;j++) {
      if (i<j && intsol[i][j] > 0) {
        sol.push_back(make_pair(i,j));
       // cout << "edge ("<<i<<","<<j<<")" << "\tof value   " <<  intsol[i][j] << endl;
      }
     /* if(j<i && intsol[j][i] > 0) {
        sol.push_back(make_pair(j,i));
        cout << "edge ("<<j<<","<<i<<")" << endl;
      }
      */
    }
  }
  //maintenant on sauvegarde ça dans "tournees"
  bool display_debug_undirected = false;
  if(display_debug_undirected)  cout << "solution undirected par tournées : "<< endl;
  int nbtournee =0;
  tournees.clear();
  vector<int> alreadyseen; //for all but sommet 0
  //parcourir toutes les tournées depuis le dépot
  for(int i = 0; i < sol.size() ; i ++) {
    if( ! vector_contains(alreadyseen, sol[i].first)  && ! vector_contains(alreadyseen, sol[i].second)) { // si pas déjà vu ce cycle
      int debutcyclecourant = sol[i].first;
      int courant = sol[i].second;
      vector<int> tempvecint; // reset
      int lasti = i; // reset
      int newcourant; //reset
      if(display_debug_undirected) cout << "("<<sol[i].first << ", " << sol[i].second << ")\t" ;
      //aller retour direct depuis 0
      if(debutcyclecourant == 0 && vectorpairintcount(sol, sol[i].second) == 1) {
        if(display_debug_undirected) cout << "cycle aller-retour depuis 0" << endl;
        alreadyseen.push_back(sol[i].second);
        tempvecint.push_back(debutcyclecourant);
        tempvecint.push_back(sol[i].second);
        tempvecint.push_back(debutcyclecourant);

        tournees.push_back(tempvecint);
        nbtournee++;
      }
      //aller retour direct entre 2 noeuds autres que le dépot
      else if(debutcyclecourant != 0 && vectorpairintcount(sol, debutcyclecourant) == 1) {
        if(display_debug_undirected) cout << "cycle aller-retour depuis 2 noeuds autres que dépot " << endl;
        alreadyseen.push_back(sol[i].first);
        alreadyseen.push_back(sol[i].second);
        tempvecint.push_back(sol[i].first);
        tempvecint.push_back(sol[i].second);
        tempvecint.push_back(sol[i].first);

        tournees.push_back(tempvecint);
        nbtournee++;
      }
      //cycle passant par le dépot
      else if(debutcyclecourant == 0) {
        if(display_debug_undirected) cout << "cycle passant par le dépot" << endl;
        alreadyseen.push_back(sol[i].second);
        tempvecint.push_back(sol[i].first);
        tempvecint.push_back(sol[i].second);
        courant = sol[i].second;
        //on parcours la tournée jusqu'à revenir en 0
        while(courant != 0) {
          std::pair<int,int> nextfound = vectorpairintfind(sol, courant, lasti);//cannot look at lasti to find the next
          if(nextfound.first == -1) {
            cout << "ERROR WITH NODE "<<courant<<" ONLY FOUND 1 BUT 2 WERE EXPECTED " << endl;
             exit(0);
          }
          newcourant = nextfound.first;
          lasti = nextfound.second;
          if(newcourant != 0) alreadyseen.push_back(newcourant);
          tempvecint.push_back(newcourant);
          courant=newcourant;
        }

        tournees.push_back(tempvecint);
        nbtournee++;
      }
      //cycle ne passant PAS par le dépot
      else {
        if(display_debug_undirected) cout << "cycle ne passant PAS par le dépot" << endl;
        alreadyseen.push_back(sol[i].first);
        alreadyseen.push_back(sol[i].second);
        tempvecint.push_back(sol[i].first);
        tempvecint.push_back(sol[i].second);
        //if(display_debug_undirected) cout << "\tdebutcycle = " <<debutcyclecourant<<endl;
        courant = sol[i].second;
        // on parcours la tournée jusqu'à revenir en debutcyclecourant
        while(courant != debutcyclecourant){
          std::pair<int,int> nextfound = vectorpairintfind(sol, courant, lasti);//cannot look at lasti to find the next
          if(nextfound.first == -1) {
            cout << "ERROR WITH NODE "<<courant<<" ONLY FOUND 1 BUT 2 WERE EXPECTED " << endl;
             exit(0);
          }
          newcourant = nextfound.first;
          lasti = nextfound.second;
          if(newcourant != 0 && newcourant != debutcyclecourant) alreadyseen.push_back(newcourant);
          tempvecint.push_back(newcourant);
        //  cout << sol[i].first <<"->"<< sol[i].second<<"->" ;
          courant = newcourant;
        }

        tournees.push_back(tempvecint);
        nbtournee++;
      }

      if(display_debug_undirected) cout << "\ttournee #"<<i<<" : "<<  vectorint_tostring(tempvecint) << endl;
      if(display_debug_undirected) cout << "\talreadyseen (size="<<alreadyseen.size()<<")= "<<vectorint_tostring(alreadyseen)<<endl; 
    }
  }

  if(display_debug_undirected) {
    //on affiche toutes les tournées et leurs couts:
    cout << "tournées : "<< endl;
    //pour chacune des tournées
    for(int i = 0; i < tournees.size(); i++) {
        cout << "\t tournée #"<<i<< " (cost= "<< G->get_route_cost_notalwaysfromDepot(tournees.at(i)) << ") (demand="<<G->get_route_demand(tournees.at(i)) <<") : ";
        //on lit toute la tournée
        for(int j = 0; j < tournees.at(i).size(); j++){
            //on extrait les sommets de la tournée à enregistrer
            cout << tournees.at(i).at(j) <<" ";
        }
        cout << endl;
    }
    //cout << "\t total_cost = " << G->get_VRP_cost_notalwaysfromDepot(tournees) << endl;
  }

  //on a toutes les tournées, on les parcours
  for(int i = 0; i < tournees.size(); i++) {
    vector<int> vectortournee = tournees.at(i);
    int tourneedemand = G->get_route_demand(tournees.at(i));

    //l'ensemble S ne doit pas contenir le Dépot pour le test de la contrainte (4)
    if(vector_contains(vectortournee, 0)) {
      vector<int> tempvec;
      //on parcours le vecteur et on supprime le dépot 0 (parfois il est au début mais parfois ailleurs)
      for(int i=0; i < vectortournee.size(); i++) {
        if(vectortournee[i] != 0) tempvec.push_back(vectortournee[i]);
      }
      vectortournee.clear();
      vectortournee.swap(tempvec); // exchange content
    }
   // cout << ">>>>" << vectorint_tostring(vectortournee) << endl; // OK VERIFIE

    //on test uniquement si la tournée est non-vide
    if( vectortournee.size() > 0) {

      //on test si la contrainte de capacité est violée
      double sumofS=0;
      for(int i=0; i<G->nb_nodes;i++){
        for(int j=0; j<G->nb_nodes;j++){
          if(i < j) {
            if(vector_contains(vectortournee, i) && ! vector_contains(vectortournee, j)
              ||vector_contains(vectortournee, j) && ! vector_contains(vectortournee, i)){
                  sumofS+=intsol[i][j];
                  //if(intsol[i][j]>0) cout << "intsol["<<i<<"]["<<j<<"] = "<<intsol[i][j] << endl;
                }
            }
        }
      }
      //cout << "partiesup("<<(float)tourneedemand/(float)G->VRP_capacity<<") = "<<(int)( ((float)tourneedemand/(float)G->VRP_capacity)+0.999)<< endl;

      int constraintval = 2 * (int)( std::ceil((double)tourneedemand/(double)G->VRP_capacity) );
      //cout << "testing capacity constraint, tournée #"<<i<<" : "<< sumofS << " >= " << constraintval << " ?" <<endl;

      if (sumofS < constraintval) {
         // contrainte de capacité violée donc on l'ajoute
          IloExpr expr(env);
          for(int i=0; i<G->nb_nodes;i++){
            for(int j=0; j<G->nb_nodes;j++){
              if(i < j ) {
                if(vector_contains(vectortournee, i) && ! vector_contains(vectortournee, j)
                  ||vector_contains(vectortournee, j) && ! vector_contains(vectortournee, i)){
                      expr+=x[i][j];
                      //cout << i << " " << j << endl;
                    }
                }
            }
          }
          
          //cout << expr << endl;

          IloRange newCte = IloRange(expr >= constraintval);
         // cout << newCte << endl;
          L_ViolatedCst.push_back(newCte);


          //cout << "\tvaleur capacité violant contrainte : " << tourneedemand << " de "<< vectortournee.size()-1<<" sommets"<< endl;
      }
    }
  }


/*
	//mirror solution to construct graph
	for(int i = 0; i < G->nb_nodes; i++) {
		for(int j = 0; j < G->nb_nodes; j++) {
			if(i > j) intsol[i][j] = intsol[j][i];
		}
	}
	*/
	//TODO 
	/*
	vector<vector<float>> fracsol;
	fracsol.resize(G->nb_nodes);
	for(int i = 0; i < G->nb_nodes; i++) {
		fracsol[i].resize(G->nb_nodes);
	}
	
	for(int i = 0; i < G->nb_nodes; i++) {
		for(int j = 0; j < G->nb_nodes; j++) {
			if(i < j) fracsol[i][j] = intsol[i][j];
			else if (i > j){
				fracsol[i][j] = intsol[j][i];
			}
		}
	}
	
	find_ViolatedCoupeMinCst(env,G,x,fracsol,L_ViolatedCst);
  */
}

// USER CUTS AVEC LES INEGALITES DE COUPES MINCUT
ILOUSERCUTCALLBACK2(UsercutCoupeMinSeparation,
         C_Graph*, G,
         vector<vector<IloNumVar>>&,x
        ){
  #ifdef OUTPUT
  cout<<"********* UserCut separation Callback: coupe-min *************"<<endl;
  #endif

  vector<vector<float>> fracsol;
  list<IloRange> L_ViolatedCst;

  fracsol.resize(G->nb_nodes);
  for(int i=0;i<G->nb_nodes;i++)
    fracsol[i].resize(G->nb_nodes);
      
  for (int i=0;i<G->nb_nodes;i++) {
      for (int j=0;j<G->nb_nodes;j++) {
        if(i!=j) {
          //cout <<"x_"<< i << "_"<<j << "\t=" <<getValue(x[i][j]) << endl;
          fracsol[i][j]= round(getValue(x[i][j]));
        }
      }
  }
 
  /* Separation of Circuit inequalities */
  
  L_ViolatedCst.clear();
  find_ViolatedCoupeMinCst(getEnv(),G,x, fracsol, L_ViolatedCst);
  
  #ifdef OUTPUT
  if (L_ViolatedCst.empty()) cout<<"No Cst found"<<endl;
  #endif
  
  while (!L_ViolatedCst.empty()){
    #ifdef OUTPUT
      cout << "Adding constraint : " << L_ViolatedCst.front() << endl;
    #endif
    add(L_ViolatedCst.front(),IloCplex::UseCutForce); //UseCutPurge);
    L_ViolatedCst.pop_front();
  }
}
//#define OUTPUT
// LAZY CUTS AVEC LES INEGALITES DE CAPACITE
ILOLAZYCONSTRAINTCALLBACK2(LazycutCapacitySeparation,
         C_Graph*, G,
         vector<vector<IloNumVar>>&,x
        ){
  #ifdef OUTPUT
  cout<<"********* Lazy separation Callback: capacity cut *************"<<endl;
  #endif

  vector<vector<int>> sol;
  list<IloRange> L_ViolatedCst;

  sol.resize(G->nb_nodes);
  for(int i=0;i<G->nb_nodes;i++)
    sol[i].resize(G->nb_nodes);
      
  for (int i=0;i<G->nb_nodes;i++) {
      for (int j=0;j<G->nb_nodes;j++) {
        sol[i][j] = 0; // init
        if(i < j) {
          if(getValue(x[i][j]) > 0 ){
            //cout <<"x_"<< i << "_"<<j << "\t= " <<getValue(x[i][j]) << endl;
          }
          sol[i][j]= round(getValue(x[i][j])) ;
          //if(getValue(x[i][j]) > 0) cout << "\t " << sol[i][j] << " =?= "<<getValue(x[i][j]) << endl;
        }
      }
  }
 
  /* Separation of Circuit inequalities */
  
  L_ViolatedCst.clear();
  find_ViolatedCapacityCst(getEnv(),G,x, sol, L_ViolatedCst);
  
  #ifdef OUTPUT
  if (L_ViolatedCst.empty()) cout<<"No Cst found"<<endl;
  #endif
  
  while (!L_ViolatedCst.empty()){
    #ifdef OUTPUT
      cout << "Adding constraint : " << L_ViolatedCst.front() << endl;
    #endif
    add(L_ViolatedCst.front(),IloCplex::UseCutForce); //UseCutPurge);
    L_ViolatedCst.pop_front();
  }

}

// LAZY CUTS AVEC LES INEGALITES DE CAPACITE
ILOLAZYCONSTRAINTCALLBACK2(LazycutCoupeMinSeparation,
         C_Graph*, G,
         vector<vector<IloNumVar>>&,x
        ){
  #ifdef OUTPUT
  cout<<"********* Lazy separation Callback: coupe-min *************"<<endl;
  #endif

  vector<vector<float>> sol;
  list<IloRange> L_ViolatedCst;

  sol.resize(G->nb_nodes);
  for(int i=0;i<G->nb_nodes;i++)
    sol[i].resize(G->nb_nodes);
      
  for (int i=0;i<G->nb_nodes;i++) {
      for (int j=0;j<G->nb_nodes;j++) {
        if(i < j) {
          //cout <<"x_"<< i << "_"<<j << "\t=" <<getValue(x[i][j]) << endl;
          sol[i][j]= round( getValue(x[i][j]) );
        }
      }
  }
 
  /* Separation of Circuit inequalities */
  
  L_ViolatedCst.clear();
  find_ViolatedCoupeMinCst(getEnv(),G,x, sol, L_ViolatedCst);
  
  #ifdef OUTPUT
  if (L_ViolatedCst.empty()) cout<<"No Cst found"<<endl;
  #endif
  
  while (!L_ViolatedCst.empty()){
    #ifdef OUTPUT
      cout << "Adding constraint : " << L_ViolatedCst.front() << endl;
    #endif
    add(L_ViolatedCst.front(),IloCplex::UseCutForce); //UseCutPurge);
    L_ViolatedCst.pop_front();
  }

}


//PLNE utilisant la formulation compacte MTZ
void optimizeMTZ(vector<vector<int>> *tournees, C_Graph* G, string filename){
	//m est le nombre de tournées
	int m = tournees->size();
	int Q = (*G).VRP_capacity; // capacité max des véhicules
	int i,j,c;

  string name=filename;
	
	IloEnv   env;
	IloModel model(env);

  cout << endl <<  "\n*** starting MTZ optimization ***\n"<< endl;

	//TODO trouver un moyen de casser la symetrie

	////////////////////////
	//////  VAR
	////////////////////////

	//DECISION
	//xij vaut 1 si l'arete (i,j) est dans une des tournees
	//wic charge restante dans le camion c au sommet i
	
	//note : pour minimiser le nombre de tournees on peut rajouter une variable tc qui vaut 1 si la tournee est non vide
	
	//DONNEES
	//cij cout de l'arete
	//Q charge max d'un vehicule
	//di demande du sommet i

	//x_i_j
	vector<vector<IloNumVar>> x;
	x.resize(G->nb_nodes);

	for (i=0; i<G->nb_nodes; i++){
    x[i].resize(G->nb_nodes);
		for (j=0; j<G->nb_nodes; j++) {
			if (i!=j){ // thomas: ne pas faire i < j ici, on veut bien créer les variables
				x[i][j]=IloNumVar(env, 0.0, 1.0, ILOINT);
				ostringstream varname;
				varname.str("");
				varname<<"x_"<<i<<"_"<<j;
        cout << varname.str() << endl;
				x[i][j].setName(varname.str().c_str());
			}
		}
	}

  // on appelle le sommet 0 le dépôt et les sommets NC = N \ {0} les revendeurs (ou clients).
	//w_i
	vector<IloNumVar> w;
	w.resize(G->nb_nodes); // prends pas en compte le sommet 0

  // thomas: tu avais fais des w_i_c mais en fait ce sont juste des w_i et on ne doit pas prendre le sommet 0

	for (i=1; i < G->nb_nodes; i++){
      w[i] = IloNumVar(env, 0.0, Q, ILOFLOAT); // 0 ≤ wi ≤ Q ∀i ∈ NC
			ostringstream varname;
			varname.str("");
			varname<<"w_"<<i;
      cout << varname.str() << endl;
			w[i].setName(varname.str().c_str());
	}

	//////////////
	//////  CST
	//////////////

	IloRangeArray CC(env);
	int nbcst=0;

	// la somme de toutes les aretes partant ou arrivant de j fait 1 (1 partant + 1 arrivant)

  //nombre max d'entrees/sorties depuis le depot <= m
  IloExpr c3(env);
  for (j=1; j < G->nb_nodes; j++){  //ok verifié
    //c3+=x[0][j]; // ok car on a tjr 0 < j
    c3 += x[0][j] ;
  }
  CC.add(c3<=m);
  ostringstream nomcst;
  nomcst.str("");
  nomcst<<"CstMax_from_depot";
  cout << nomcst.str() << endl;
  CC[nbcst].setName(nomcst.str().c_str());
  nbcst++;
  
  
  IloExpr c4(env);
  for (i=1; i < G->nb_nodes; i++){   //ok verifié
    c4 += x[i][0];
  }
  CC.add(c4<=m);
  ostringstream nomcst2;
  nomcst2.str("");
  nomcst2<<"CstMax_to_depot";
  cout << nomcst2.str() << endl;
  CC[nbcst].setName(nomcst2.str().c_str());
  nbcst++;
  

  for (i=1; i < G->nb_nodes; i++){ // ∀i ∈ NC
    IloExpr c2(env);
    for (j=0; j < G->nb_nodes; j++){
      if(i!=j) {
        c2+=x[i][j];
      }
    }
    CC.add(c2==1);
    ostringstream nomcst;
    nomcst.str("");
    nomcst<<"Cst_from_"<<i;
    cout << nomcst.str() << endl;
    CC[nbcst].setName(nomcst.str().c_str());
    nbcst++;
  }

	for (j=1; j < G->nb_nodes; j++){ // ∀i ∈ NC
		IloExpr c1(env);
		for (i=0; i < G->nb_nodes; i++){
			if (i != j){
         c1+=x[i][j];
       }
		}
		CC.add(c1==1);
		ostringstream nomcst;
		nomcst.str("");
		nomcst<<"Cst_to_"<<j;
    cout << nomcst.str() << endl;
		CC[nbcst].setName(nomcst.str().c_str());
		nbcst++;
	}
	
	// contraintes MTZ (pour i quelconque et j!=0)
	for (i=1; i < G->nb_nodes; i++){

		int di = G->V_nodes[i].VRP_demand;

		for (j=0; j < G->nb_nodes; j++){
  		if (i!=j)
      {
  				IloExpr c5(env);
          if(j!=0)
    			  c5 += w[i] - w[j] - di +  (Q + di ) * ( 1 - x[i][j] ); 
          else
            c5 += w[i] - 0    - di +  (Q + di ) * ( 1 - x[i][j] ); 

          cout << "w_"<<i<<" - w_"<<j<<" - "<<di << " + ("<<Q+di<<" * (1-xij) ) >= 0"<<endl;

  				CC.add(c5>=0);
  				ostringstream nomcst3;
  				nomcst3.str("");
  				nomcst3<<"Cst_MTZ_"<<i<<"_"<<j;
  				CC[nbcst].setName(nomcst3.str().c_str());
  				nbcst++;
			}
		}
	}



	model.add(CC);


	//////////////
	////// OBj
	//////////////

  // min sum( cij * xij ) pour tout (i,j)€A 

	IloObjective obj=IloAdd(model, IloMinimize(env, 0.0));

  //ici on prend bien en compte les arêtes partants et arrivants au sommet source 0
	for (i=0; i < G->nb_nodes; i++){
		for (j=0; j < G->nb_nodes; j++){
			if (i!=j) {// previously if(i!=j) 
        obj.setLinearCoef(x[i][j], G->lengthTSP(i,j));
        //obj.setLinearCoef(x[i][j], 0); // affiche la première solution trouvée valide
    }
   }
  }
			
			
	///////////
	//// RESOLUTION
	//////////

	
	//cout << CC << endl; //print all constraints
	
	IloCplex cplex(model);
  
	cplex.setParam(IloCplex::TiLim, CPX_PARAM_TILIM);
	
  /// ADD SEPARATION CALLBACK
  //cplex.use(LazyCoupeMinSeparation(env,G,x)); // TODO

  cplex.use(UsercutCoupeMinSeparation(env,G,x));

	// cplex.setParam(IloCplex::Cliques,-1);
	// cplex.setParam(IloCplex::Covers,-1);
	// cplex.setParam(IloCplex::DisjCuts,-1);
	// cplex.setParam(IloCplex::FlowCovers,-1);
	// cplex.setParam(IloCplex::FlowPaths,-1);
	// cplex.setParam(IloCplex::FracCuts,-1);
	// cplex.setParam(IloCplex::GUBCovers,-1);
	// cplex.setParam(IloCplex::ImplBd,-1);
	// cplex.setParam(IloCplex::MIRCuts,-1);
	// cplex.setParam(IloCplex::ZeroHalfCuts,-1);
	// cplex.setParam(IloCplex::MCFCuts,-1);
	// cplex.setParam(IloCplex::MIPInterval,1);
	// cplex.setParam(IloCplex::HeurFreq,-1);
	// cplex.setParam(IloCplex::ClockType,1);
	// cplex.setParam(IloCplex::RINSHeur,-1);


	string sortielp = /*filename+*/"sortie.lp";
	cplex.exportModel(sortielp.c_str());

	if ( !cplex.solve() ) {
		env.error() << "Failed to optimize LP" << endl;
		exit(1);
	}


	env.out() << "Solution status = " << cplex.getStatus() << endl;
	env.out() << "Solution value  = " << cplex.getObjValue() << endl;
	

  //affichage des w_i
  /*cout<<"---"<<endl;
  for(int i = 1; i < G->nb_nodes; i++) {
    cout <<"w_" << i<<" = " << cplex.getValue(w[i]) << endl;
  }
  cout<<"---"<<endl;
  */

  // on enregistre tous les arcs du graphe dans sol
  // et on va aussi exporter la solution dans le pointeur d'entrée "tournees"
  //cout << "\n MTZ arcs in graph solution : " << endl;
	list<pair<int,int>> sol; // marche pour tous
  map<int, int> solmap; // marche pour tous sauf le depot (car il y a plusieurs entrées avec le mm depot)
	for(i=0; i < G->nb_nodes; i++) {
		for (j=0; j < G->nb_nodes;j++) {
			if (i!=j && cplex.getValue(x[i][j])>1-epsilon) {
				sol.push_back(make_pair(i,j));
        if(i != 0) solmap.insert(make_pair(i,j));
        //cout << "arc ("<<i<<","<<j<<")" << endl;
      }
    }
  }
  //maintenant on sauvegarde ça dans "tournees"
  bool display_debug_mtz = false;
  if(display_debug_mtz)  cout << "solution MTZ par tournées : "<< endl;
  int nbtournee =0;
  tournees->clear();
  //parcourir toutes les tournées depuis le dépot
  for(auto it = sol.begin(); it != sol.end(); ++it) {
    if(it->first == 0) {
      vector<int> tempvecint;
      //quand on trouve une tournée passant par 0 on la parcours jusqu'à revenir à 0
      int courant = it->second;
      if(display_debug_mtz) cout << "tournée : "<< it->first;
      while(courant != 0) {
        tempvecint.push_back(courant);
          if(display_debug_mtz) cout << "\t -> " << courant << "\td="<< G->get_node_by_id_startat0(courant)->VRP_demand << "\tw="<< cplex.getValue(w[courant]) << endl;
          courant = solmap.find(courant)->second;
      }
      if(display_debug_mtz) cout << "\t -> 0"<< endl;
      tournees->push_back(tempvecint);
      nbtournee++;
    }
  }


  //END CPLEX
	env.end();

}


//PLNE utilisant la formulation non dirigée
void optimize_undirected(vector<vector<int>> *tournees, C_Graph* G, string filename, double *sol_time, double * sol_value){
	//m est le nombre de tournées
	int m = tournees->size();
	int Q = (*G).VRP_capacity; // capacité max des véhicules
	int i,j,c;

	G->directed = false;
	
  string name=filename;
	
	IloEnv   env;
	IloModel model(env);

  cout << endl <<  "\n*** starting undirected optimization ***\n"<< endl;

	////////////////////////
	//////  VAR
	////////////////////////

	//DECISION
	//xij vaut 1 si l'arete (i,j) est dans une des tournees
	//wi charge restante dans le camion c au sommet i
	
	//note : pour minimiser le nombre de tournees on peut rajouter une variable tc qui vaut 1 si la tournee est non vide
	
	//DONNEES
	//cij cout de l'arete
	//Q charge max d'un vehicule
	//di demande du sommet i

	//x_i_j
	vector<vector<IloNumVar>> x;
	x.resize(G->nb_nodes);

	for (i=0; i<G->nb_nodes; i++){
    x[i].resize(G->nb_nodes);
		for (j=0; j<G->nb_nodes; j++) {
			if (i<j){ // baptiste : TEST
				//x[i][j]=IloNumVar(env, 0.0, (!i || !j ? 2.0 : 1.0), ILOINT); // si i ou j == 0 -> 2
        if(i==0 || j==0)  x[i][j]=IloNumVar(env, 0.0, 2.0, ILOINT); 
        else              x[i][j]=IloNumVar(env, 0.0, 1.0, ILOINT); 

				ostringstream varname;
				varname.str("");
				varname<<"x_"<<i<<"_"<<j;
        cout << varname.str() << endl;
				x[i][j].setName(varname.str().c_str());
			}
		}
	}

	/*
  // on appelle le sommet 0 le dépôt et les sommets NC = N \ {0} les revendeurs (ou clients).
	//w_i
	vector<IloNumVar> w;
	w.resize(G->nb_nodes); // ne prend pas en compte le sommet 0

	for (i=1; i < G->nb_nodes; i++){
      w[i] = IloNumVar(env, 0.0, Q, ILOFLOAT); // 0 ≤ wi ≤ Q ∀i ∈ NC
			ostringstream varname;
			varname.str("");
			varname<<"w_"<<i;
      cout << varname.str() << endl;
			w[i].setName(varname.str().c_str());
	}

	*/
	
	//////////////
	//////  CST
	//////////////

	IloRangeArray CC(env);
	int nbcst=0;

	// la somme de toutes les aretes partant ou arrivant de j fait 1 (1 partant + 1 arrivant)

  //nombre max d'entrees/sorties depuis le depot == 2m
  // contrainte (2)
  IloExpr c3(env);
  for (j=1; j < G->nb_nodes; j++){
    c3 += x[0][j] ;
    //cout << ">>> " << 0 << "  " << j << endl; 
	  //c3 += x[j][0] ;
  }
  cout << " == "  << 2*m << endl;
  CC.add(c3 == 2*m);
  ostringstream nomcst;
  nomcst.str("");
  nomcst<<"CstMax_from_depot";
  cout << nomcst.str() << endl;
  CC[nbcst].setName(nomcst.str().c_str());
  nbcst++;
  
  /*		//cf ci-dessus
  IloExpr c4(env);
  for (i=1; i < G->nb_nodes; i++){   //ok verifié
    c4 += x[i][0];
  }
  CC.add(c4<=m);
  ostringstream nomcst2;
  nomcst2.str("");
  nomcst2<<"CstMax_to_depot";
  cout << nomcst2.str() << endl;
  CC[nbcst].setName(nomcst2.str().c_str());
  nbcst++;
  */

  // contrainte (3)
  for (i=1; i < G->nb_nodes; i++){ // ∀i ∈ NC
    IloExpr c2(env);
    for (j=0; j < G->nb_nodes; j++){
      if(i < j) {
        //cout << " >>> "<< i << " " << j << endl;
        c2+=x[i][j];
      }
      else if(j < i){
        //cout << " >>> "<< j << " " << i << endl;
  	   	c2+=x[j][i];
  	  }
    }
    CC.add(c2==2);
    ostringstream nomcst;
    nomcst.str("");
    nomcst<<"Cst_sum_" << i ;
    cout << nomcst.str() << endl;
    CC[nbcst].setName(nomcst.str().c_str());
    nbcst++;
  }

  /*
	for (j=1; j < G->nb_nodes; j++){ // ∀i ∈ NC
		IloExpr c1(env);
		for (i=0; i < G->nb_nodes; i++){
			if (i != j){
         c1+=x[i][j];
       }
		}
		CC.add(c1==1);
		ostringstream nomcst;
		nomcst.str("");
		nomcst<<"Cst_to_"<<j;
    cout << nomcst.str() << endl;
		CC[nbcst].setName(nomcst.str().c_str());
		nbcst++;
	}
	*/
  
  /*
	// contraintes MTZ (pour i quelconque et j!=0)
	for (i=1; i < G->nb_nodes; i++){

		int di = G->V_nodes[i].VRP_demand;

		for (j=0; j < G->nb_nodes; j++){
  		if (i!=j)
      {
  				IloExpr c5(env);
          if(j!=0)
    			  c5 += w[i] - w[j] - di +  (Q + di ) * ( 1 - x[i][j] ); 
          else
            c5 += w[i] - 0    - di +  (Q + di ) * ( 1 - x[i][j] ); 

          cout << "w_"<<i<<" - w_"<<j<<" - "<<di << " + ("<<Q+di<<" * (1-xij) ) >= 0"<<endl;

  				CC.add(c5>=0);
  				ostringstream nomcst3;
  				nomcst3.str("");
  				nomcst3<<"Cst_MTZ_"<<i<<"_"<<j;
  				CC[nbcst].setName(nomcst3.str().c_str());
  				nbcst++;
			}
		}
	}
*/
  
  


	model.add(CC);


	//////////////
	////// OBj
	//////////////

  // min sum( cij * xij ) pour tout (i,j)€A 

	IloObjective obj=IloAdd(model, IloMinimize(env, 0.0));

  //ici on prend bien en compte les arêtes partants et arrivants au sommet source 0
	for (i=0; i < G->nb_nodes; i++){
		for (j=0; j < G->nb_nodes; j++){
			if (i<j) {// previously if(i!=j) 
        obj.setLinearCoef(x[i][j], G->lengthTSP(i,j));
        //obj.setLinearCoef(x[i][j], 0); // affiche la première solution trouvée valide
    }
   }
  }
			
			
	///////////
	//// RESOLUTION
	//////////

	
	//cout << CC << endl; //print all constraints


	
	IloCplex cplex(model);
  
	cplex.setParam(IloCplex::TiLim, CPX_PARAM_TILIM);
	
	IloNum start;
	start=cplex.getTime();
	
  /// ADD SEPARATION CALLBACK
	
	cplex.use(LazycutCapacitySeparation(env,G,x));
	//cplex.use(LazycutCoupeMinSeparation(env,G,x));
	
	
	
	// cplex.setParam(IloCplex::Cliques,-1);
	// cplex.setParam(IloCplex::Covers,-1);
	// cplex.setParam(IloCplex::DisjCuts,-1);
	// cplex.setParam(IloCplex::FlowCovers,-1);
	// cplex.setParam(IloCplex::FlowPaths,-1);
	// cplex.setParam(IloCplex::FracCuts,-1);
	// cplex.setParam(IloCplex::GUBCovers,-1);
	// cplex.setParam(IloCplex::ImplBd,-1);
	// cplex.setParam(IloCplex::MIRCuts,-1);
	// cplex.setParam(IloCplex::ZeroHalfCuts,-1);
	// cplex.setParam(IloCplex::MCFCuts,-1);
	// cplex.setParam(IloCplex::MIPInterval,1);
	// cplex.setParam(IloCplex::HeurFreq,-1);
	// cplex.setParam(IloCplex::ClockType,1);
	// cplex.setParam(IloCplex::RINSHeur,-1);

	if ( !cplex.solve() ) {
		env.error() << "Failed to optimize LP" << endl;
		exit(1);
	}

	string sortielp = /*filename+*/"sortie.lp";
	cplex.exportModel(sortielp.c_str());

	env.out() << "Solution status = " << cplex.getStatus() << endl;
	env.out() << "Solution value  = " << cplex.getObjValue() << endl;
	
	*sol_value = cplex.getObjValue();
	*sol_time = cplex.getTime() - start;

  //affichage des w_i
  /*cout<<"---"<<endl;
  for(int i = 1; i < G->nb_nodes; i++) {
    cout <<"w_" << i<<" = " << cplex.getValue(w[i]) << endl;
  }
  cout<<"---"<<endl;
  */

  //on va aussi exporter la solution dans le pointeur d'entrée "tournees"
  //cout << "\n edges in undirected graph solution : " << endl;
	vector<pair<int,int>> sol; // marche pour tous
	for(i=0; i < G->nb_nodes; i++) {
		for (j=0; j < G->nb_nodes;j++) {
			if (i<j && cplex.getValue(x[i][j])>1-epsilon) {
				sol.push_back(make_pair(i,j));
        //cout << "edge ("<<i<<","<<j<<")" << endl;
      }
    }
  }
  //maintenant on sauvegarde ça dans "tournees"
  bool display_debug_undirected = false;
  if(display_debug_undirected)  cout << "solution undirected par tournées : "<< endl;
  int nbtournee =0;
  tournees->clear();
  vector<int> alreadyseen; //for all but sommet 0
  //parcourir toutes les tournées depuis le dépot
  for(int i = 0; i < sol.size(); i ++) {
    if(sol[i].first == 0 && ! vector_contains(alreadyseen, sol[i].second) ) { // si pas déjà vu ce cycle
      vector<int> tempvecint; // la tournée 
      //quand on trouve une tournée passant par 0 on la parcours jusqu'à revenir à 0
      int courant = sol[i].second;
      if(display_debug_undirected) cout << "tournée : "<< sol[i].first;
      int lasti = i;
      while(courant != 0 && ! vector_contains(alreadyseen, courant)) {
          alreadyseen.push_back(courant);
          tempvecint.push_back(courant);
          if(display_debug_undirected) cout << "\t -> " << courant << "\td="<< G->get_node_by_id_startat0(courant)->VRP_demand << endl;
          int newcourant;
          //on commence par regarder si il y a au moins 2 edges avec le sommet courrant (un de chaque coté)
          int counterfound= vectorpairintcount(sol,courant);
          //cout << "val "<<courant<<" found "<<counterfound<<" times"<<endl;
          if(counterfound >= 2) { // cycle de au moins 2+ sommets
            std::pair<int,int> found = vectorpairintfind(sol, courant,lasti);
            newcourant = found.first;
            lasti = found.second;
           // cout << "newcourant = "<<newcourant << endl;
          }
          else{ // aller retour direct depuis 0
            alreadyseen.push_back(courant);
            newcourant = 0;
          }
          //if(newcourant != 0)  alreadyseen.push_back(newcourant);
          //cout << "alreadyseen = " << vectorint_tostring(alreadyseen) << endl;

          //cout << courant << " to " << newcourant << endl;
          courant = newcourant;
      }
      if(display_debug_undirected) cout << "\t -> 0"<< endl;
      tournees->push_back(tempvecint);
      nbtournee++;
    }
  }


  //END CPLEX
	env.end();

}



int main (int argc, char**argv){
  //////////////////////////////
  ///////// PARAMETERS /////////
  //////////////////////////////
  bool relaxedPLNE_activateprint  = false;
  bool relaxedPLNE_activateoutput = false;
  ////////////////////////////// end of parameters

  std::srand(std::time(0)); // seed for random,very important

  vector<int> solution_vec;

  if(argc!=2){
    cerr<<"usage: "<<argv[0]<<" filename.vrp"<<endl; 
    return 1;
  }
  string filename = argv[1];

  C_Graph* G = parseVRPfile(filename, relaxedPLNE_activateprint);
  //C_node* cnode = graph_ptr->get_node_by_id(1);    //exemple d'utilisation
  //(*G).nb_nodes;    // G->nb_nodes;                //both mean the same here
  //C_node* cnode  = (*G).get_node_by_id_startat1(1);   //example
  int capaciteQ = (*G).VRP_capacity; // capacité max des véhicules

  int nb_box_used;

  vector<vector<int>> tournees;
  vector<vector<int>> best_tournees; //only used when we run it several time if it is too fast

   //on solve par PLNE après avoir relaxé la contrainte de m tournées
   nb_box_used = solve_relaxedPLNE(G, filename, &solution_vec, relaxedPLNE_activateprint, relaxedPLNE_activateoutput); //last paramter is write_outputs?, befre-last is print?

  //pour chacune des tournées
  tournees.clear();
  for(int i = 0; i < nb_box_used; i++) {
      vector<int> curr_tournee;
      //on lit tout le vecteur solution
      for(int j = 0; j < solution_vec.size(); j++){
          //on extrait les sommets de la tournée à enregistrer
          if(solution_vec.at(j) == i+1) {
              curr_tournee.push_back(j+1); //+1 pour commencer à 1 au lieu de 0
          }
      }
      std::random_shuffle ( curr_tournee.begin(), curr_tournee.end() );  //shuffle optimise bcp la vitesse moyenne !
      tournees.push_back(curr_tournee);
  }
  std::random_shuffle(tournees.begin(), tournees.end());//shuffle optimise bcp la vitesse moyenne !

    cout << endl;
  //print solution found by relaxed PLNE
  cout << "NB BOXES USED : " << nb_box_used << endl;
  cout << "SOLUTION : ";
  for(int i = 0; i < solution_vec.size(); i++)
        cout << solution_vec.at(i) << " " ;
  cout << endl;
  // remarque : la solution trouvée ne contient que des valeurs qui se suivent à partir de 1 : 1,2,3,4,5...

  //resolution heuristique 2opt
  ///*
  // let it run at least X sec if result found too fast (=relaunch)
  int nbsecmin = 2; //use 0 to launch only once
  int starttime = std::time(nullptr);
  
  int elapsed_time = std::time(nullptr) - starttime;
  float all_best_VRPcost = INT_MAX;
  int count = 0;
  while(elapsed_time < nbsecmin || count == 0) { //lance au moins 1 fois
    count ++;
    ///////////////////////////
      //on solve par PLNE après avoir relaxé la contrainte de m tournées
        nb_box_used = solve_relaxedPLNE(G, filename, &solution_vec, relaxedPLNE_activateprint, relaxedPLNE_activateoutput); //last paramter is write_outputs?, befre-last is print?


        /////////////////////////////////////////////////////////////////////////////////////
        //////////////// métaheuristique itérative par voisinage ////////////////////////////
        /////////////////////////////////////////////////////////////////////////////////////
        //
        //  A l’issue d’une étape initiale, on obtient une solution réalisable.
        //  Une métaheuristique itérative peut alors être utilisée sur la base de plusieurs voisinages:
        //  - les voisinages classiques du TSP (2-opt) pour améliorer chaque tournée indépendemment.
        //  - la possiblité pour un client de changer de tournées
        //  - supprimer une tournée vide
        //  - éventuellement ajouter une tournée vide 
        //  OBj: minimiser la somme toale des couts des arcs utilisés (sous contrainte de capacité des véhicules)
        //  rmq: on peut décrire une tournée par une liste de sommets (i0, i1, ...ip) comme le graphe est complet.
        //       tous les circuits commencent par le sommet 0 et finissent au sommet 0
        //  TODO: -> peut-être ajouter métaheuristique en ajoutant une tournée vide au début qu'on supprime ensuite ?-> testé mais change rien ?
        //
        //création du vecteur de vecteurs contenant toutes les tournées (sans le sommet 0):
        //pour chacune des tournées
        tournees.clear();
        for(int i = 0; i < nb_box_used; i++) {
            vector<int> curr_tournee;
            //on lit tout le vecteur solution
            for(int j = 0; j < solution_vec.size(); j++){
                //on extrait les sommets de la tournée à enregistrer
                if(solution_vec.at(j) == i+1) {
                    curr_tournee.push_back(j+1); //+1 pour commencer à 1 au lieu de 0
                }
            }
            std::random_shuffle ( curr_tournee.begin(), curr_tournee.end() );  //shuffle optimise bcp la vitesse moyenne !
            tournees.push_back(curr_tournee);
        }
        std::random_shuffle(tournees.begin(), tournees.end());//shuffle optimise bcp la vitesse moyenne !
        //test : ajout d'une tournée vide
        //vector<int> tournee_vide;
        //tournees.push_back(tournee_vide);

        //on affiche toutes les tournées et leurs couts:
        cout<<endl; print_all_tournees(tournees, G);

        float previous_best_VRPcost = G->get_VRP_cost(tournees);
        float current_best_VRPcost  = G->get_VRP_cost(tournees);
    ////////////////////////////

    int nb_stagnations = 0; // use 10 for a pretty exhaustive search
    //tant qu'on arrive à améliorer par 2opt on continue (jusqu'à avoir stagné nb_stagnations fois)

    do {
      if(previous_best_VRPcost <= current_best_VRPcost) nb_stagnations++;
      else nb_stagnations = 0;

      //voisinage 2opt pour optimiser chaque tournée indépendament
      optimize_2opt_internalRoutes(&tournees, G);
     // print_all_tournees(tournees, G);

      //voisinage 2opt pour optimiser entre les tournées
      optimize_2opt_switchRoutes(&tournees, G);
      //print_all_tournees(tournees, G);

      //MAJ les valeurs
      previous_best_VRPcost = current_best_VRPcost;
      current_best_VRPcost = G->get_VRP_cost(tournees);
    }while (nb_stagnations < 10);		// si on voit que 10x de suite on à le même cout on arrête 

    if(current_best_VRPcost < all_best_VRPcost) {
      cout << "new best solution found of cost " <<current_best_VRPcost  << endl;
      all_best_VRPcost = current_best_VRPcost; //maj val
      //on copie les tournées dans best_tournees
      best_tournees.clear();
      for(int i =0; i < tournees.size(); i++){
          vector<int> curr_tournee;
          //on lit tout le vecteur solution
          for(int j = 0; j < tournees[i].size(); j++){
              curr_tournee.push_back(tournees[i][j]);
          }
          best_tournees.push_back(curr_tournee);
      }
    }
    elapsed_time = std::time(nullptr) - starttime; 
    cout << "ELAPSED TIME = "<<elapsed_time<<" SECONDS / "<<nbsecmin << endl;

  } // while timer

  //on affiche toutes les tournées et leurs couts:
  cout << "after 2opt optimization (did "<<count<<" iterations): " << endl;
  
  cout<<endl; print_all_tournees(best_tournees, G);
  //*/ // FIN HEURISTIQUE
  
  ///////////////////////////////////////////////////////////////////////////////////// fin métaheuristique itérative par voisinage
  
  /*
  //optimizeMTZ_withsymmetry(&tournees, G, filename); // without any CUTS
  optimizeMTZ(&tournees, G, filename); //reinforced with CUTS
  //on affiche toutes les tournées et leurs couts:
  cout << "after MTZ optimization : " << endl;
  cout<<endl; print_all_tournees(tournees, G);
  //*/
  /*
  double sol_time, sol_value;
  optimize_undirected(&tournees, G, filename, &sol_time, &sol_value);
  //on affiche toutes les tournées et leurs couts:
  cout << "after undirected optimization : " << endl;
  cout<<endl; print_all_tournees(tournees, G);
  write_results_to_file(filename, sol_time, sol_value);
  //*/
  //write to svg
  write_solution_to_file(tournees, G, filename);
  
  return 0;
}

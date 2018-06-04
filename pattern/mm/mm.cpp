/////////////////////////////////////////////////////////////////////////////
//
// mm.cpp
//
// Rémi Coulom
//
// February, 2007
//
/////////////////////////////////////////////////////////////////////////////
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <cmath>
#include <fstream>
#include <assert.h>

const double PriorVictories = 1.0;
const double PriorGames = 2.0;
const double PriorOpponentGamma = 1.0;

/////////////////////////////////////////////////////////////////////////////
// One "team": product of gammas
/////////////////////////////////////////////////////////////////////////////
class CTeam
{
 private: ///////////////////////////////////////////////////////////////////
  static std::vector<int> vi;
  int Index;
  int Size;

 public: ////////////////////////////////////////////////////////////////////
  CTeam(): Index(vi.size()), Size(0) {}
  int GetSize() const {return Size;}
  int GetIndex(int i) const {return vi[Index+i];}
  void Append(int i) {vi.push_back(i); Size++;}
};

std::vector<int> CTeam::vi;

int
gamma_to_feature(int gamma, std::vector<int> &vFeatureIndex)
{
	for (unsigned int i = 0; i < vFeatureIndex.size(); i++) {
		if (vFeatureIndex[i] > gamma)
			return i;
	}
	return vFeatureIndex.size();
}

/////////////////////////////////////////////////////////////////////////////
// Read a team
/////////////////////////////////////////////////////////////////////////////
CTeam ReadTeam(std::string &s, std::vector<int> &vFeatureIndex, int Gammas)
{
 std::istringstream in(s);
 CTeam team;
 int Index;
 while(1)
 {
  in >> Index;
  if (Index < 0 || Index >= Gammas) {
	  std::cerr << '\n' << s << '\n';
	  fprintf(stderr, "invalid gamma: %i\n", Index);
	  assert(0);
  }
  if (in) {
	  int feature = gamma_to_feature(Index, vFeatureIndex);
	  for (int i = team.GetSize(); --i >= 0;) {
		  if (feature == gamma_to_feature(team.GetIndex(i), vFeatureIndex)) {
			  std::cerr << '\n' << s << '\n';
			  fprintf(stderr, "%i and %i are same feature !\n", Index, team.GetIndex(i));
			  assert(0);
		  }
	  }
	  team.Append(Index);
  }
  else
   break;
 }
 return team;
}

/////////////////////////////////////////////////////////////////////////////
// One "Game": One winner team out of several participants
/////////////////////////////////////////////////////////////////////////////
class CGame
{
 public: ////////////////////////////////////////////////////////////////////
  CTeam Winner;
  std::vector<CTeam> vParticipants;
};

/////////////////////////////////////////////////////////////////////////////
// Game Collection:
/////////////////////////////////////////////////////////////////////////////
class CGameCollection
{
 public: ////////////////////////////////////////////////////////////////////
  std::vector<CGame> vgame;
  std::vector<double> vGamma;
  std::vector<int> vFeatureIndex;
  std::vector<std::string> vFeatureName;
  std::vector<double> vVictories;
  std::vector<int> vParticipations;
  std::vector<int> vPresences;

  void ComputeVictories();
  void MM(int Feature);
  double LogLikelihood() const;

  double GetTeamGamma(const CTeam &team) const
  {
   double Result = 1.0;
   for (int i = team.GetSize(); --i >= 0;)
    Result *= vGamma[team.GetIndex(i)];
   return Result;
  }
};

/////////////////////////////////////////////////////////////////////////////
// Compute log likelihood
/////////////////////////////////////////////////////////////////////////////
double CGameCollection::LogLikelihood() const
{
 double L = 0;

 for (int i = vgame.size(); --i >= 0;)
 {
  const CGame &game = vgame[i];
  double Opponents = 0; 
  const std::vector<CTeam> &v = game.vParticipants;
  for (int j = v.size(); --j >= 0;)
   Opponents += GetTeamGamma(v[j]);
  L += std::log(GetTeamGamma(game.Winner));
  L -= std::log(Opponents);
 }

 return L;
}

/////////////////////////////////////////////////////////////////////////////
// Compute victories for each gamma (and games played)
/////////////////////////////////////////////////////////////////////////////
void CGameCollection::ComputeVictories()
{
 vVictories.resize(vGamma.size());
 vParticipations.resize(vGamma.size());
 vPresences.resize(vGamma.size());

 for (int i = vVictories.size(); --i >= 0;)
 {
  vVictories[i] = 0;
  vParticipations[i] = 0;
  vPresences[i] = 0;
 }

 for (int i = vgame.size(); --i >= 0;)
 {
  const CTeam &Winner = vgame[i].Winner;
  for (int j = Winner.GetSize(); --j >= 0;)
   vVictories[Winner.GetIndex(j)]++;

  int tParticipations[vGamma.size()];
  for (int j = vGamma.size(); --j >= 0;)
   tParticipations[j] = 0;

  for (int k = vgame[i].vParticipants.size(); --k >= 0;)
   for (int j = vgame[i].vParticipants[k].GetSize(); --j >= 0;)
   {
    int Index = vgame[i].vParticipants[k].GetIndex(j);
    vParticipations[Index]++;
    tParticipations[Index]++;
   }

  for (int i = vGamma.size(); --i >= 0;)
   if (tParticipations[i])
    vPresences[i]++;
 }

#if 0
 for (int i = vGamma.size(); --i >= 0;)
  std::cerr << i << ' ' << vVictories[i] << '\n';
#endif
}

/////////////////////////////////////////////////////////////////////////////
// One iteration of minorization-maximization, for one feature
/////////////////////////////////////////////////////////////////////////////
void CGameCollection::MM(int Feature)
{
 //
 // Interval for this feature
 //
 int Max = vFeatureIndex[Feature + 1];
 int Min = vFeatureIndex[Feature];

 //
 // Compute denominator for each gamma
 //
 std::vector<double> vDen(vGamma.size());
 for (int i = vDen.size(); --i >= 0;)
  vDen[i] = 0.0;

 //
 // Main loop over games
 //
 std::map<int,double> tMul;
 for (int i = vgame.size(); --i >= 0;)
 {
  //double tMul[vGamma.size()];
  //{
  // for (int i = vGamma.size(); --i >= 0;)
  //  tMul[i] = 0.0;
  //}
  tMul.clear();

  double Den = 0.0;

  std::vector<CTeam> &v = vgame[i].vParticipants;
  for (int i = v.size(); --i >= 0;)
  {
   const CTeam &team = v[i];

   double Product = 1.0;
   int FeatureIndex = -1;

   for (int i = 0; i < team.GetSize(); i++)
   {
    int Index = team.GetIndex(i);
    if (Index >= Min && Index < Max)
     FeatureIndex = Index;
    else
     Product *= vGamma[Index];
   }

   if (FeatureIndex >= 0)
   {
    //tMul[FeatureIndex] += Product;
    if (tMul.count(FeatureIndex))
    	tMul[FeatureIndex] += Product;
    else
	tMul[FeatureIndex]=Product;
    
    Product *= vGamma[FeatureIndex];
   }

   Den += Product;
  }

  //for (int i = Max; --i >= Min;)
  // vDen[i] += tMul[i] / Den;
  for (std::map<int,double>::iterator it=tMul.begin();it!=tMul.end();++it)
  {
   int key=it->first;
   vDen[key]+=it->second / Den;
  }
 }

 //
 // Update Gammas
 //
 for (int i = Max; --i >= Min;)
 {
  double NewGamma = (vVictories[i] + PriorVictories) /
                    (vDen[i] + PriorGames / (vGamma[i] + PriorOpponentGamma));
  vGamma[i] = NewGamma;
 }
}

/////////////////////////////////////////////////////////////////////////////
// Read game collection
/////////////////////////////////////////////////////////////////////////////
void ReadGameCollection(CGameCollection &gcol, std::istream &in)
{
 //
 // Read number of gammas in the first line
 //
 int MaxGamma;
 {
  std::string sLine;
  std::getline(in, sLine);
  std::istringstream is(sLine);
  std::string s;
  int Gammas = 0;
  is >> s >> Gammas;
  MaxGamma = Gammas;
  gcol.vGamma.resize(Gammas);
  for (int i = Gammas; --i >= 0;)
   gcol.vGamma[i] = 1.0;
 }

 //
 // Features
 //
 {
  gcol.vFeatureIndex.push_back(0);
  int Features = 0;
  in >> Features;
  for (int i = 0; i < Features; i++)
  {
   int Gammas;
   in >> Gammas;
   int Min = gcol.vFeatureIndex.back();
   gcol.vFeatureIndex.push_back(Min + Gammas);
   std::string sName;
   in >> sName;
   gcol.vFeatureName.push_back(sName);
  }
 }

 //
 // Main loop over games
 //
 std::string sLine;
 std::getline(in, sLine);

 while(in)
 {
  //
  // Parse a game
  //
  if (sLine == "#")
  {
   CGame game;

   //
   // Winner
   //
   std::getline(in, sLine);
   game.Winner = ReadTeam(sLine, gcol.vFeatureIndex, MaxGamma);

   //
   // Participants
   //
   std::getline(in, sLine);
   while (sLine[0] != '#' && sLine[0] != '!' && in)
   {
    CTeam team = ReadTeam(sLine, gcol.vFeatureIndex, MaxGamma);
    game.vParticipants.push_back(team);
    std::getline(in, sLine);
   }

   gcol.vgame.push_back(game);
  }
  else
  {
   std::getline(in, sLine);
   std::cerr << '.';
  }
 }
 std::cerr << '\n';
}

/////////////////////////////////////////////////////////////////////////////
// Write ratings
/////////////////////////////////////////////////////////////////////////////
void WriteRatings(const CGameCollection &gcol,
                  std::ostream &out,
                  int fExtraData)
{
 for (unsigned i = 0; i < gcol.vGamma.size(); i++)
 {
  out << std::setw(3) << i << ' ' << std::setw(10) << gcol.vGamma[i] << ' ';
  if (fExtraData)
  {
   out << std::setw(11) << gcol.vVictories[i];
   out << std::setw(11) << gcol.vParticipations[i];
   out << std::setw(11) << gcol.vPresences[i];
  }
  out << '\n';
 }
}

/////////////////////////////////////////////////////////////////////////////
// main function
/////////////////////////////////////////////////////////////////////////////
int main()
{
 CGameCollection gcol;
 ReadGameCollection(gcol, std::cin);
 gcol.ComputeVictories();
 std::cerr << "Games = " << gcol.vgame.size() << '\n';
 double LogLikelihood = gcol.LogLikelihood() / gcol.vgame.size();

 const int Features = gcol.vFeatureName.size();
 double tDelta[Features];

 for (int k = 2; --k >= 0;)
 {
  for (int i = Features; --i >= 0;)
   tDelta[i] = 10.0;

  while(1)
  {
   //
   // Select feature with max delta
   //
   int Feature = 0;
   double MaxDelta = tDelta[0];
   for (int j = Features; --j > 0;)
    if (tDelta[j] > MaxDelta)
     MaxDelta = tDelta[Feature = j];
   if (MaxDelta < 0.0001)
    break;
   
   //
   // Run one MM iteration over this feature
   //
   std::cerr << std::setw(20) << gcol.vFeatureName[Feature] << ' ';
   std::cerr << std::setw(9) << LogLikelihood << ' ';
   std::cerr << std::setw(9) << std::exp(-LogLikelihood) << ' ';
   gcol.MM(Feature);
   double NewLogLikelihood = gcol.LogLikelihood() / gcol.vgame.size();
   double Delta = NewLogLikelihood - LogLikelihood;
   tDelta[Feature] = Delta;
   std::cerr << std::setw(9) << Delta << '\n';
   LogLikelihood = NewLogLikelihood;
  }
 }

 WriteRatings(gcol, std::cout, 0);

 {
  std::ofstream ofs("mm-with-freq.dat");
  WriteRatings(gcol, ofs, 1);
 }
 
 return 0;
}

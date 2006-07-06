#include "precompiled.h"
#include "NUSpline.h"
#include "Matrix3D.h"

//Note: column major order!  Each set of 4 constitutes a column. 
CMatrix3D HermiteSpline(  2.f, -3.f, 0.f, 1.f, 
						 -2.f, 3.f, 0.f, 0.f, 
						 1.f, -2.f, 1.f, 0.f, 
					     1.f, -1.f, 0.f, 0.f );  // Matrix H in article


// cubic curve defined by 2 positions and 2 velocities
CVector3D GetPositionOnCubic(const CVector3D &startPos, const CVector3D &startVel, const CVector3D &endPos, const CVector3D &endVel, float time)
{
  CMatrix3D m( startPos.X, endPos.X, startVel.X, endVel.X, 
				startPos.Y, endPos.Y, startVel.Y, endVel.Y,
		        startPos.Z, endPos.Z, startVel.Z, endVel.Z, 
			    0.0f, 0.0f, 0.0f, 1.0f );
  
  m = m * HermiteSpline; // multiply by the mixer


  CVector3D TimeVector(time*time*time, time*time, time);
  CVector3D Result;
  m.Transform(TimeVector, Result);
  return Result;
}

/*********************************** R N S **************************************************/

// adds node and updates segment length
void RNSpline::AddNode(const CVector3D &pos)
{
  if ( NodeCount >= MAX_SPLINE_NODES )
	  return;
  if (NodeCount == 0)
    MaxDistance = 0.f;
  else
  {
    Node[NodeCount-1].Distance = (Node[NodeCount-1].Position - pos).GetLength();
    MaxDistance += Node[NodeCount-1].Distance;
  }
  SplineData temp;
  temp.Position = pos;
  Node.push_back(temp);
  NodeCount++;
}


// called after all nodes added. This function calculates the node velocities
void RNSpline::BuildSpline()
{
  if ( NodeCount == 2 )
  {
	  Node[0].Velocity = GetStartVelocity(0);
      Node[NodeCount-1].Velocity = GetEndVelocity(NodeCount-1);
	  return;
  }
  else if ( NodeCount < 2 )
	  return;
  
  for (int i = 1; i<NodeCount-1; i++)
  {
    CVector3D Next = Node[i+1].Position - Node[i].Position;
	CVector3D Previous = Node[i-1].Position - Node[i].Position;
	Next.Normalize();
	Previous.Normalize();
	 
	// split the angle (figure 4)
    Node[i].Velocity = Next - Previous;
    Node[i].Velocity.Normalize();
  }
  // calculate start and end velocities
  Node[0].Velocity = GetStartVelocity(0);
  Node[NodeCount-1].Velocity = GetEndVelocity(NodeCount-1);
}

// spline access function. time is 0 -> 1
CVector3D RNSpline::GetPosition(float time)
{
  if ( NodeCount < 2 )
	  return CVector3D(0.0f, 0.0f, 0.0f);
  if ( time > 1.0f )
		time = 1.0f;
  float Distance = time * MaxDistance;
  float CurrentDistance = 0.f;
  int i = 0;
  
  //Find which node we're on
  while (CurrentDistance + Node[i].Distance < Distance
    && i < NodeCount - 2)
  {
    CurrentDistance += Node[i].Distance;
    i++;
  }
  debug_assert( i < NodeCount - 1 ); 
  float t = Distance - CurrentDistance;
  t /= Node[i].Distance; // scale t in range 0 - 1
  CVector3D startVel = Node[i].Velocity * Node[i].Distance;
  CVector3D endVel = Node[i+1].Velocity * Node[i].Distance;   
  return GetPositionOnCubic(Node[i].Position, startVel,
                         Node[i+1].Position, endVel, t);
}

// internal. Based on Equation 14 
CVector3D RNSpline::GetStartVelocity(int index)
{
  if ( index >= NodeCount - 1 || index < 0)
	  return CVector3D(0.0f, 0.0f, 0.0f);
  CVector3D temp = (Node[index+1].Position - Node[index].Position) * 3.0f * ( 1.0f / Node[index].Distance) ;
  return (temp - Node[index+1].Velocity)*0.5f;
}

// internal. Based on Equation 15 
CVector3D RNSpline::GetEndVelocity(int index)
{
  if ( index >= NodeCount || index < 1)
	  return CVector3D(0.0f, 0.0f, 0.0f);
  CVector3D temp = (Node[index].Position - Node[index-1].Position) * 3.0f * (1.0f / Node[index-1].Distance);
  return (temp - Node[index-1].Velocity) * 0.5f;
}

/*********************************** S N S **************************************************/

// smoothing filter.
void SNSpline::Smooth()
{
  if ( NodeCount < 3 )
	  return;
  CVector3D newVel;
  CVector3D oldVel = GetStartVelocity(0);
  for (int i = 1; i<NodeCount-1; i++)
  {
    // Equation 12
    newVel = GetEndVelocity(i) * Node[i].Distance +
             GetStartVelocity(i) * Node[i-1].Distance;
    newVel = newVel * ( 1 / (Node[i-1].Distance + Node[i].Distance) );
    Node[i-1].Velocity = oldVel;
    oldVel = newVel;
  }
  Node[NodeCount-1].Velocity = GetEndVelocity(NodeCount-1);
  Node[NodeCount-2].Velocity = oldVel;
}

/*********************************** T N S **************************************************/

// as with RNSpline but use timePeriod in place of actual node spacing
// ie time period is time from last node to this node
void TNSpline::AddNode(const CVector3D &pos, float timePeriod)
{
  if ( NodeCount >= MAX_SPLINE_NODES )
	  return;
  if (NodeCount == 0)
     MaxDistance = 0.f;
  else
  {
    Node[NodeCount-1].Distance = timePeriod;
    MaxDistance += Node[NodeCount-1].Distance;
  }
  SplineData temp;
  temp.Position = pos;
  //make sure we don't end up using undefined numbers...
  temp.Distance = 0.0f;
  temp.Velocity = CVector3D( 0.0f, 0.0f, 0.0f );
  Node.push_back(temp);
  NodeCount++;
}

//Inserts node before position
void TNSpline::InsertNode(const int index, const CVector3D &pos, float timePeriod)
{
  if ( NodeCount >= MAX_SPLINE_NODES || index < NodeCount - 1  )
	  return;
  if (NodeCount == 0)
      MaxDistance = 0.f;
  else
  {
    Node[NodeCount-1].Distance = timePeriod;
    MaxDistance += Node[NodeCount-1].Distance;
  }
  SplineData temp;
  temp.Position = pos;
  Node.insert(Node.begin() + index, temp);
  NodeCount++;
}
//Removes node at index
void TNSpline::RemoveNode(const int index)
{
  if (NodeCount == 0 || index > NodeCount - 1 )
  {
     return;
  }
  else
  {
	  MaxDistance -= Node[index].Distance;
	  MaxDistance -= Node[index-1].Distance;
	  Node[index-1].Distance = 0.0f;
	  Node.erase( Node.begin() + index, Node.begin() + index + 1 );
  }
  NodeCount--;
}
void TNSpline::UpdateNodeTime(const int index, float time)
{ 
	if (NodeCount == 0 || index > NodeCount - 1 )
	{
		 return;
	}
	Node[index].Distance = time; 
}
void TNSpline::UpdateNodePos(const int index, const CVector3D &pos)
{ 
	if (NodeCount == 0 || index > NodeCount - 1 )
	{
		 return;
	}
	Node[index].Position = pos; 
}
void TNSpline::Constrain()
{
  if ( NodeCount < 3 )
	  return;
  for (int i = 1; i<NodeCount-1; i++)
  {
    // Equation 13
    float r0 = (Node[i].Position-Node[i-1].Position).GetLength() / Node[i-1].Distance;
    float r1 = (Node[i+1].Position - Node[i].Position).GetLength() / Node[i].Distance;
    Node[i].Velocity *= 4.0f*r0*r1/((r0+r1)*(r0+r1));
  }
}


// Last modified: May 15 2004, Mark Thompson (mark@wildfiregames.com)

#include "precompiled.h"

#include "ps/Profile.h"

#include "Entity.h"
#include "EntityManager.h"
#include "BaseEntityCollection.h"
#include "graphics/Unit.h"
#include "Aura.h"
#include "ProductionQueue.h"
#include "renderer/Renderer.h"
#include "graphics/Model.h"
#include "graphics/Terrain.h"
#include "ps/Interact.h"
#include "Collision.h"
#include "PathfindEngine.h"
#include "ps/Game.h"
#include "maths/scripting/JSInterface_Vector3D.h"
#include "maths/MathUtil.h"
#include "ps/CConsole.h"
#include "renderer/WaterManager.h"
#include "EntityFormation.h"
#include "simulation/FormationManager.h"
#include "Formation.h"
#include "graphics/GameView.h"
#include "graphics/Sprite.h"
#include "graphics/UnitManager.h"

extern CConsole* g_Console;
extern int g_xres, g_yres;

#include <algorithm>
using namespace std;
std::map<CStr, size_t> CEntity::m_AttributeTable;

CEntity::CEntity( CBaseEntity* base, CVector3D position, float orientation, const std::set<CStrW>& actorSelections, CStrW building )
{
	if(m_AttributeTable.size() == 0)	// not nice
	{
		initAttributes(this);
	}

    m_position = position;
    m_orientation.Y = orientation;
    m_ahead.x = sin( m_orientation.Y );
    m_ahead.y = cos( m_orientation.Y );
	m_player = 0;

	/* Anything added to this list MUST be added to BaseEntity.cpp (and variables used should
		also be added to BaseEntity.h */
	
    AddProperty( L"actions.move.speed_curr", &m_speed );
	AddProperty( L"actions.move.run.speed", &m_runSpeed );
	AddProperty( L"actions.move.run.rangemin", &( m_run.m_MinRange ) );
	AddProperty( L"actions.move.run.range", &( m_run.m_MaxRange ) );
	AddProperty( L"actions.move.run.regen_rate", &m_runRegenRate );
	AddProperty( L"actions.move.run.decay_rate", &m_runDecayRate );
	AddProperty( L"actions.move.pass_through_allies", &m_passThroughAllies );
    AddProperty( L"selected", &m_selected, false, (NotifyFn)&CEntity::checkSelection );
    AddProperty( L"group", &m_grouped, false, (NotifyFn)&CEntity::checkGroup );
    AddProperty( L"traits.extant", &m_extant );
    AddProperty( L"actions.move.turningradius", &m_turningRadius );
    AddProperty( L"position", &m_graphics_position, false, (NotifyFn)&CEntity::teleport );
    AddProperty( L"orientation", &(m_orientation.Y), false, (NotifyFn)&CEntity::reorient );
    AddProperty( L"player", &m_player, false, (NotifyFn)&CEntity::playerChanged );
    AddProperty( L"traits.health.curr", &m_healthCurr );
    AddProperty( L"traits.health.max", &m_healthMax );
    AddProperty( L"traits.health.bar_height", &m_healthBarHeight );
	AddProperty( L"traits.health.bar_size", &m_healthBarSize );
	AddProperty( L"traits.health.bar_width", &m_healthBarWidth );
	AddProperty( L"traits.health.border_height", &m_healthBorderHeight);
	AddProperty( L"traits.health.border_width", &m_healthBorderWidth );
	AddProperty( L"traits.health.border_name", &m_healthBorderName );
	AddProperty( L"traits.health.regen_rate", &m_healthRegenRate );
	AddProperty( L"traits.health.regen_start", &m_healthRegenStart );
	AddProperty( L"traits.health.decay_rate", &m_healthDecayRate );
	AddProperty( L"traits.stamina.curr", &m_staminaCurr );
    AddProperty( L"traits.stamina.max", &m_staminaMax );
    AddProperty( L"traits.stamina.bar_height", &m_staminaBarHeight );
	AddProperty( L"traits.stamina.bar_size", &m_staminaBarSize );
	AddProperty( L"traits.stamina.bar_width", &m_staminaBarWidth );
	AddProperty( L"traits.stamina.border_height", &m_staminaBorderHeight);
	AddProperty( L"traits.stamina.border_width", &m_staminaBorderWidth );
	AddProperty( L"traits.stamina.border_name", &m_staminaBorderName );
	AddProperty( L"traits.rally.name", &m_rallyTexture );
	AddProperty( L"traits.rally.width", &m_rallyWidth );
	AddProperty( L"traits.rally.height", &m_rallyHeight );
	AddProperty( L"traits.flank_penalty.sectors", &m_sectorDivs);
	AddProperty( L"traits.pitch.sectors", &m_pitchDivs );
	AddProperty( L"traits.rank.width", &m_rankWidth );
	AddProperty( L"traits.rank.height", &m_rankHeight );
	AddProperty( L"traits.rank.name", &m_rankName );
    AddProperty( L"traits.minimap.type", &m_minimapType );
    AddProperty( L"traits.minimap.red", &m_minimapR );
    AddProperty( L"traits.minimap.green", &m_minimapG );
    AddProperty( L"traits.minimap.blue", &m_minimapB );
    AddProperty( L"traits.anchor.type", &m_anchorType );
	AddProperty( L"traits.anchor.conformx", &m_anchorConformX );
	AddProperty( L"traits.anchor.conformz", &m_anchorConformZ );
    AddProperty( L"traits.vision.los", &m_los );
    AddProperty( L"traits.vision.permanent", &m_permanent );
	AddProperty( L"last_combat_time", &m_lastCombatTime );
	AddProperty( L"last_run_time", &m_lastRunTime );
	AddProperty( L"building", &m_building );

	m_productionQueue = new CProductionQueue( this );
	AddProperty( L"production_queue", m_productionQueue );

    for( int t = 0; t < EVENT_LAST; t++ )
    {
        AddProperty( EventNames[t], &m_EventHandlers[t], false );
        AddHandler( t, &m_EventHandlers[t] );
    }

    m_collisionPatch = NULL;

    // Set our parent unit and build us an actor.
    m_actor = NULL;
    m_bounds = NULL;

    m_lastState = -1;
    m_transition = true;
    m_fsm_cyclepos = NOT_IN_CYCLE;

    m_base = base;

	m_actorSelections = actorSelections;
	loadBase();

	m_sectorValues.resize(m_sectorDivs);
	for ( int i=0; i<m_sectorDivs; ++i )
		m_sectorValues[i] = false;
	
    if( m_bounds )
        m_bounds->setPosition( m_position.X, m_position.Z );

    m_position_previous = m_position;
    m_orientation_previous = m_orientation;

    m_graphics_position = m_position;
    m_graphics_orientation = m_orientation;
    m_actor_transform_valid = false;
	m_hasRallyPoint = false;

    m_destroyed = false;

    m_selected = false;
	m_isRunning = false;
	m_shouldRun = false;
	m_triggerRun = false;

	m_healthDecay = false;

	m_frameCheck = 0;
	m_lastCombatTime = 0;
	m_lastRunTime = 0;
	m_currentNotification = 0;
	m_currentRequest = 0;
	m_destroyNotifiers = true;

	m_formationSlot = -1;
	m_formation = -1;
    m_grouped = -1;

	m_building = building;
	
    m_player = g_Game->GetPlayer( 0 );
}

CEntity::~CEntity()
{
    if( m_actor )
    {
        g_UnitMan.RemoveUnit( m_actor );
        delete( m_actor );
    }

    if( m_bounds )
    {
        delete( m_bounds );
    }

	delete m_productionQueue;

    for( AuraTable::iterator it = m_auras.begin(); it != m_auras.end(); it++ )
    {
        delete it->second;
    }
    m_auras.clear();
	
	m_destroyNotifiers=true;
	for ( size_t i=0; i<m_listeners.size(); i++ )
		m_listeners[i].m_sender->DestroyNotifier( this );
	DestroyAllNotifiers();

	CEntity* remove = this;
	g_FormationManager.RemoveUnit(remove);
}

void CEntity::loadBase()
{
    if( m_actor )
    {
        g_UnitMan.RemoveUnit( m_actor );
        delete( m_actor );
        m_actor = NULL;
    }
    if( m_bounds )
    {
        delete( m_bounds );
        m_bounds = NULL;
    }

    CStr actorName ( m_base->m_actorName ); // convert CStrW->CStr8

	m_actor = g_UnitMan.CreateUnit( actorName, this, m_actorSelections );

    // Set up our instance data

    SetBase( m_base );
    m_classes.SetParent( &( m_base->m_classes ) );
    SetNextObject( m_base );

    if( m_base->m_bound_type == CBoundingObject::BOUND_CIRCLE )
    {
        m_bounds = new CBoundingCircle( m_position.X, m_position.Z, m_base->m_bound_circle );
    }
    else if( m_base->m_bound_type == CBoundingObject::BOUND_OABB )
    {
        m_bounds = new CBoundingBox( m_position.X, m_position.Z, m_ahead, m_base->m_bound_box );
    }

    m_actor_transform_valid = false;

	if( m_player )
	{
		// Make sure the actor has the right player colour
		m_actor->SetPlayerID( m_player->GetPlayerID() );
	}
}

void CEntity::initAttributes(const CEntity* _this)
{
#define getoffset(member) \
	(size_t) ((unsigned char*)&_this->member - (unsigned char*)_this) 

#define getoffset_action(member, mem2) \
	(size_t) ((unsigned char*)&_this->member.mem2 - (unsigned char*)_this) 
	
	//Add the attribute name and the variable that holds it
	CEntity::m_AttributeTable["actions.move.speed_curr"] = getoffset(m_speed);
	CEntity::m_AttributeTable["actions.move.run.speed"] = getoffset(m_runSpeed);
	CEntity::m_AttributeTable["actions.move.run.rangemin"] = getoffset_action(m_run, m_MinRange);
	CEntity::m_AttributeTable["actions.move.run.range"] = getoffset_action(m_run, m_MaxRange);
	CEntity::m_AttributeTable["actions.move.run.regen_rate"] = getoffset(m_runRegenRate);
	CEntity::m_AttributeTable["actions.move.run.decay_rate"] = getoffset(m_runDecayRate);
	CEntity::m_AttributeTable["actions.move.pass_through_allies"] = getoffset(m_passThroughAllies);
    CEntity::m_AttributeTable["traits.extant"] = getoffset(m_extant);
    CEntity::m_AttributeTable["actions.move.turningradius"] = getoffset(m_turningRadius);

    CEntity::m_AttributeTable["traits.health.curr"] = getoffset(m_healthCurr);
    CEntity::m_AttributeTable["traits.health.max"] = getoffset(m_healthMax);
	CEntity::m_AttributeTable["traits.health.regen_rate"] = getoffset(m_healthRegenRate);
	CEntity::m_AttributeTable["traits.health.regen_start"] = getoffset(m_healthRegenStart);
	CEntity::m_AttributeTable["traits.health.decay_rate"] = getoffset(m_healthDecayRate);
	//This are not changable from techs until the updated bars are finished
	
	/*	CEntity::m_AttributeTable["traits.stamina.curr"] = getoffset(m_staminaCurr);
    CEntity::m_AttributeTable["traits.stamina.max"] = getoffset(m_staminaMax);
    CEntity::m_AttributeTable["traits.bars.height"] = getoffset(m_barHeight);
	CEntity::m_AttributeTable["traits.bars"] = getoffset(m_barOffset);
	CEntity::m_AttributeTable["traits.bars.width"] = getoffset(m_barWidth);
	CEntity::m_AttributeTable["traits.bars.border_height"] = getoffset(m_barBorderHeight);
	CEntity::m_AttributeTable["traits.bars.border_width"] = getoffset(m_barBorderWidth);
	CEntity::m_AttributeTable["traits.bars.border_name"] = getoffset(m_barBorderName);*/
	CEntity::m_AttributeTable["traits.flank_penalty.sectors"] = getoffset(m_sectorDivs);
	CEntity::m_AttributeTable["traits.pitch.sectors"] = getoffset(m_pitchDivs);
	CEntity::m_AttributeTable["traits.rank.width"] = getoffset(m_rankWidth);
//	CEntity::m_AttributeTable["traits.rank"] = getoffset(m_rankOffset);
	CEntity::m_AttributeTable["traits.rank.height"] = getoffset(m_rankHeight);
	CEntity::m_AttributeTable["traits.rank.name"] = getoffset(m_rankName);
    CEntity::m_AttributeTable["traits.minimap.type"] = getoffset(m_minimapType);
    CEntity::m_AttributeTable["traits.minimap.red"] = getoffset(m_minimapR);
    CEntity::m_AttributeTable["traits.minimap.green"] = getoffset(m_minimapG);
    CEntity::m_AttributeTable["traits.minimap.blue"] = getoffset(m_minimapB);
    CEntity::m_AttributeTable["traits.anchor.type"] = getoffset(m_anchorType);
	CEntity::m_AttributeTable["traits.anchor.conformx"] = getoffset(m_anchorConformX);
	CEntity::m_AttributeTable["traits.anchor.conformz"] = getoffset(m_anchorConformZ);
    CEntity::m_AttributeTable["traits.vision.los"] = getoffset(m_los);
    CEntity::m_AttributeTable["traits.vision.permanent"] = getoffset(m_permanent);
	CEntity::m_AttributeTable["last_combat_time"] = getoffset(m_lastCombatTime);
	CEntity::m_AttributeTable["last_run_time"] = getoffset(m_lastRunTime);
	CEntity::m_AttributeTable["building"] = getoffset(m_building);
#undef getoffset
}
void CEntity::kill()
{
    g_Selection.removeAll( me );

	CEntity* remove = this;
	g_FormationManager.RemoveUnit(remove);
	
	m_destroyNotifiers=true;
	for ( size_t i=0; i<m_listeners.size(); i++ )
		m_listeners[i].m_sender->DestroyNotifier( this );
	DestroyAllNotifiers();

	if( m_bounds )
        delete( m_bounds );
    m_bounds = NULL;

    m_extant = false;

    m_destroyed = true;
    //Shutdown(); // PT: tentatively removed - this seems to be called by ~CJSComplex, and we don't want to do it twice

    if( m_actor )
    {
        g_UnitMan.RemoveUnit( m_actor );
        delete( m_actor );
        m_actor = NULL;
    }

    updateCollisionPatch();


    me = HEntity(); // will deallocate the entity, assuming nobody else has a reference to it
}

void CEntity::SetPlayer(CPlayer *pPlayer)
{
    m_player = pPlayer;

    // Store the ID of the player in the associated model
    if( m_actor )
        m_actor->SetPlayerID( m_player->GetPlayerID() );
}

void CEntity::updateActorTransforms()
{
    CMatrix3D m;
	CMatrix3D mXZ;
	float Cos = cosf( m_graphics_orientation.Y );
	float Sin = sinf( m_graphics_orientation.Y );

	m._11=-Cos;  m._12=0.0f; m._13=-Sin;  m._14=0.0f;
	m._21=0.0f; m._22=1.0f; m._23=0.0f; m._24=0.0f;
	m._31=Sin; m._32=0.0f; m._33=-Cos;  m._34=0.0f;
	m._41=0.0f; m._42=0.0f; m._43=0.0f; m._44=1.0f;
  
	mXZ.SetXRotation( m_graphics_orientation.X );
	mXZ.RotateZ( m_graphics_orientation.Z );
	mXZ = m*mXZ;
	mXZ.Translate(m_graphics_position);

    if( m_actor )
        m_actor->GetModel()->SetTransform( mXZ );
}

void CEntity::snapToGround()
{
    m_graphics_position.Y = getAnchorLevel( m_graphics_position.X, m_graphics_position.Z );
}

jsval CEntity::getClassSet()
{
    STL_HASH_SET<CStrW, CStrW_hash_compare>::iterator it;
    it = m_classes.m_Set.begin();
    CStrW result = L"";
    if( it != m_classes.m_Set.end() )
    {
        result = *( it++ );
        for( ; it != m_classes.m_Set.end(); it++ )
            result += L" " + *it;
    }
    return( ToJSVal( result ) );
}

void CEntity::setClassSet( jsval value )
{
    // Get the set that was passed in.
    CStr temp = ToPrimitive<CStrW>( value );
    CStr entry;

    m_classes.m_Added.clear();
    m_classes.m_Removed.clear();

    while( true )
    {
        long brk_sp = temp.Find( ' ' );
        long brk_cm = temp.Find( ',' );
        long brk = ( brk_sp == -1 ) ? brk_cm : ( brk_cm == -1 ) ? brk_sp : ( brk_sp < brk_cm ) ? brk_sp : brk_cm;

        if( brk == -1 )
        {
            entry = temp;
        }
        else
        {
            entry = temp.GetSubstring( 0, brk );
            temp = temp.GetSubstring( brk + 1, temp.Length() );
        }

        if( brk != 0 )
        {

            if( entry[0] == '-' )
            {
                entry = entry.GetSubstring( 1, entry.Length() );
                if( entry.Length() )
                    m_classes.m_Removed.push_back( entry );
            }
            else
            {
                if( entry[0] == '+' )
                    entry = entry.GetSubstring( 1, entry.Length() );
                if( entry.Length() )
                    m_classes.m_Added.push_back( entry );
            }
        }
        if( brk == -1 )
            break;
    }

    rebuildClassSet();
}

void CEntity::rebuildClassSet()
{
    m_classes.Rebuild();
    InheritorsList::iterator it;
    for( it = m_Inheritors.begin(); it != m_Inheritors.end(); it++ )
        (*it)->rebuildClassSet();
}

void CEntity::update( size_t timestep )
{
    m_position_previous = m_position;
    m_orientation_previous = m_orientation;
	
	CalculateRun( timestep );
	CalculateHealth( timestep );

	if ( m_triggerRun )
		m_frameCheck++;

	if ( m_frameCheck != 0 )
	{
		m_shouldRun = true;
		m_triggerRun = false;
		m_frameCheck = 0;
	}

	m_productionQueue->Update( timestep );

	// Note: aura processing is done before state processing because the state
    // processing code is filled with all kinds of returns

    PROFILE_START( "aura processing" );

    for( AuraTable::iterator it = m_auras.begin(); it != m_auras.end(); it++ )
    {
        it->second->Update( timestep );
    }

    PROFILE_END( "aura processing" );

    // The process[...] functions return 'true' if the order at the top of the stack
    // still needs to be (re-)evaluated; else 'false' to terminate the processing of
    // this entity in this timestep.

    PROFILE_START( "state processing" );

	if( m_isRunning )
	{
		m_lastRunTime = g_Game->GetTime();
	}

    while( !m_orderQueue.empty() )
	{
        CEntityOrder* current = &m_orderQueue.front();

        if( current->m_type != m_lastState )
        {
            m_transition = true;
            m_fsm_cyclepos = NOT_IN_CYCLE;

            PROFILE( "state transition / order" );

            CEntity* target = NULL;
            if( current->m_data[0].entity )
                target = &( *( current->m_data[0].entity ) );

            CVector3D worldPosition = (CVector3D)current->m_data[0].location;

            CEventOrderTransition evt( m_lastState, current->m_type, target, worldPosition );

            if( !DispatchEvent( &evt ) )
            {
                m_orderQueue.pop_front();
                continue;
            }
            else if( target )
            {
                current->m_data[0].location = worldPosition;
                current->m_data[0].entity = target->me;
            }

            m_lastState = current->m_type;
        }
        else
		{
            m_transition = false;
		}

        switch( current->m_type )
        {
            case CEntityOrder::ORDER_GOTO_NOPATHING:
            case CEntityOrder::ORDER_GOTO_COLLISION:
            case CEntityOrder::ORDER_GOTO_SMOOTHED:
				if( processGotoNoPathing( current, timestep ) )
					break;
				updateCollisionPatch();
				return;
            case CEntityOrder::ORDER_GENERIC:
				if( processGeneric( current, timestep ) )
					break;
				updateCollisionPatch();
				return;
			case CEntityOrder::ORDER_START_CONSTRUCTION:
				{
					CEventStartConstruction evt( current->m_data[0].entity );
					m_orderQueue.pop_front();
					DispatchEvent( &evt );
				}
				break;
            case CEntityOrder::ORDER_PRODUCE:
				processProduce( current );
				m_orderQueue.pop_front();
				break;
            case CEntityOrder::ORDER_GENERIC_NOPATHING:
				if( processGenericNoPathing( current, timestep ) )
					break;
				updateCollisionPatch();
				return;
			case CEntityOrder::ORDER_GOTO_WAYPOINT:
				if ( processGotoWaypoint( current, timestep, false ) )
					break;
				updateCollisionPatch();
				return;
			case CEntityOrder::ORDER_GOTO_WAYPOINT_CONTACT:
				if ( processGotoWaypoint( current, timestep, true ) )
					break;
				updateCollisionPatch();
				return;
            case CEntityOrder::ORDER_GOTO:
			case CEntityOrder::ORDER_RUN:
				if( processGoto( current, timestep ) )
					break;
				updateCollisionPatch();
				return;
            case CEntityOrder::ORDER_PATROL:
				if( processPatrol( current, timestep ) )
					break;
				updateCollisionPatch();
				return;
            case CEntityOrder::ORDER_PATH_END_MARKER:
				m_orderQueue.pop_front();
				break;
            default:
				debug_warn( "Invalid entity order" );
		}
    }

	if( m_orderQueue.empty() ) 
	{
		// If we have no orders, stop running
		m_isRunning = false;
		m_shouldRun = false;
	}

    PROFILE_END( "state processing" );

	// If we get to here, it means we're idle or dead (no orders); update the animation

    if( m_actor )
    {
        PROFILE( "animation updates" );
        if( m_extant )
        {
            if( ( m_lastState != -1 ) || !m_actor->GetModel()->GetAnimation() )
			{
				m_actor->SetEntitySelection( L"idle" );
                m_actor->SetRandomAnimation( "idle" );
			}
        }
    }

    if( m_lastState != -1 )
    {
        PROFILE( "state transition event" );
        CEntity* d0;
        CVector3D d1;
        CEventOrderTransition evt( m_lastState, -1, d0, d1 );
        DispatchEvent( &evt );

        m_lastState = -1;
    }
}

void CEntity::updateCollisionPatch()
{
    vector<CEntity*>* newPatch = g_EntityManager.getCollisionPatch( this );
    if( newPatch != m_collisionPatch )
    {
        if( m_collisionPatch )
        {
            // remove ourselves from old patch
            vector<CEntity*>& old = *m_collisionPatch;
            if( old.size() == 1 )
            {
                // we were the only ones there, just pop us
                old.pop_back();
            }
            else
            {
                // find our location and put in the last guy in the patch, then pop back
                for( size_t i=0; i < old.size(); i++ )
                {
                    if( old[i] == this )
                    {
                        old[i] = old.back();
                        old.pop_back();
                        break;
                    }
                }
            }
        }

        if( m_extant )
        {
            // add ourselves to new patch
            newPatch->push_back( this );
            m_collisionPatch = newPatch;
        }
    }
}

#if AURA_TEST
void CEntity::UpdateAuras( size_t timestep_millis )
{
    std::vector<SAura>::iterator it_a;
    for( it_a = m_Auras.begin(); it_a != m_Auras.end(); it_a++ )
    {
        SAuraData& d = it_a->m_Data;
        std::set<CEntity*>
        & inRange = GetEntitiesWithinRange( m_position, d.m_Radius );

        std::vector<CEntity*>::iterator it1 = inRange.begin();
        std::vector<SAuraInstance>::iterator it2 = it_a->m_Influenced.begin();

        while( WORLD_IS_ROUND )
        {
            if( it1 == inRange.end() )
            {
                // No more in range => anything else in the influenced set must have gone
                // out of range.
                for( ; it2 != it_a->m_Influenced.end(); it2++ )
                    UpdateAuras_LeaveRange( *it_a, it2->GetEntity() );

                break;
            }
            if( it2 == it_a->m_Influenced.end() )
            {
                // Everything else in the in-range set has only just come into range
                for( ; it1 != inRange.end(); it1++ )
                    UpdateAuras_EnterRange( *it_a, *it );
                break;
            }
            CEntity* e1 = *it1, e2 = it2->GetEntity();
            if( e1 < e2 )
            {
                // A new entity e1 has just come into range.
                // Check to see if it can be affected by the aura.
                UpdateAuras_EnterRange( *it_a, e1 );
                ++it1;
            }
            else if( e1 == e2 )
            {
                // The entity e1/e2 was previously in range, and still is.
                UpdateAuras_Normal( *it_a, e1 );
                ++it1;
                ++it2;
            }
            else
            {
                // The entity e2 was previously in range, but is no longer.
                UpdateAuras_LeaveRange( *it_a, e2 );
                ++it2;
            }
        }
    }
}

void UpdateAuras_EnterRange( SAura& aura, CEntity* e )
{
    if( aura.m_Recharge )
        return( false );
    // Check to see if the entity is eligable
    if( !UpdateAuras_IsEligable( aura.m_Data, e ) )
        return; // Do nothing.

    SAuraInstance ai;
    ai.m_Influenced = e;
    ai.m_EnteredRange = ai.m_LastInRange = 0;
    ai.m_Applied = -1;

    // If there's no timer, apply the effect now.
    if( aura.m_Data.m_Time == 0 )
    {
        e->ApplyAuraEffect( aura.m_Data );
        ai.m_Applied = 0;
        aura.m_Recharge = aura.m_Data.m_Cooldown;
    }

    aura.m_Influenced.push_back( ai );
}

void UpdateAuras_Normal( SAura& aura, CEntity* e )
{
    // Is the entity no longer eligable?
    if( !UpdateAuras_IsEligable( aura.m_Data, e ) )
    {
        //}
    }

    bool UpdateAuras_IsEligable( SAuraData& aura, CEntity* e )
    {
        if( e == this )
        {
            if( !( aura.m_Allegiance & SAuraData::SELF ) )
                return( false );
        }
        else if( e->m_player == GetGaiaPlayer() )
        {
            if( !( aura.m_Allegiance & SAuraData::GAIA ) )
                return( false );
        }
        else if( e->m_player == m_player )
        {
            if( !( aura.m_Allegiance & SAuraData::PLAYER ) )
                return( false );
        }
        // TODO: Allied players
        else
        {
            if( !( aura.m_Allegiance & SAuraData::ENEMY ) )
                return( false );
        }
        if( e->m_hp > e->m_hp_max * aura.m_Hitpoints )
            return( false );
        return( true );
    }

#endif

bool CEntity::Initialize()
{
    CEventInitialize evt;
    if( !DispatchEvent( &evt ) ) 
	{
		kill();
		return false;
	}
	return true;
}

void CEntity::Tick()
{
    CEventTick evt;
    DispatchEvent( &evt );
}

void CEntity::clearOrders()
{
    if ( m_orderQueue.empty() )
		return;
	CIdleEvent evt( m_orderQueue.front(), m_currentNotification );
	DispatchEvent(&evt);
	m_orderQueue.clear();
}
void CEntity::popOrder()
{
	if ( m_orderQueue.empty() )
		return;
	CIdleEvent evt( m_orderQueue.front(), m_currentNotification );
	DispatchEvent(&evt);

	m_orderQueue.pop_front();
}
void CEntity::pushOrder( CEntityOrder& order )
{
	CEventPrepareOrder evt( order.m_data[0].entity, order.m_type, order.m_data[1].data, order.m_data[0].string );
	if( DispatchEvent(&evt) )
    {
		m_orderQueue.push_back( order );
		if(evt.m_notifyType != CEntityListener::NOTIFY_NONE)
		{
			CheckListeners( evt.m_notifyType, evt.m_notifySource );
		}
    }
}

void CEntity::DispatchNotification( CEntityOrder order, int type )
{
	CEventNotification evt( order, type );
	DispatchEvent( &evt );
}

struct isListenerSender
{
	CEntity* sender;
	isListenerSender(CEntity* sender) : sender(sender) {}
	bool operator()(CEntityListener& listener)
	{
		return listener.m_sender == sender;
	}
};

int CEntity::DestroyNotifier( CEntity* target )
{
	if ( target->m_listeners.empty() )
		return 0;
	//Stop listening
	// (Don't just loop and use 'erase', because modifying the deque while
	// looping over it is a bit dangerous)
	std::deque<CEntityListener>::iterator newEnd = std::remove_if(
		target->m_listeners.begin(), target->m_listeners.end(),
		isListenerSender(this));
	target->m_listeners.erase(newEnd, target->m_listeners.end());

	//Get rid of our copy
	std::vector<CEntity*>::iterator newEnd2 = std::remove_if(
		m_notifiers.begin(), m_notifiers.end(),
		bind2nd(std::equal_to<CEntity*>(), target));
	int removed = std::distance(newEnd2, m_notifiers.end());
	m_notifiers.erase(newEnd2, m_notifiers.end());
	return removed;
}
void CEntity::DestroyAllNotifiers()
{
	debug_assert(m_destroyNotifiers);
	//Make them stop listening to us
	while ( ! m_notifiers.empty() )
		DestroyNotifier( m_notifiers[0] );
}
CEntityFormation* CEntity::GetFormation()
{
	if ( m_formation < 0 )
		return NULL;
	return g_FormationManager.GetFormation(m_formation);
}
void CEntity::DispatchFormationEvent( int type )
{
	CFormationEvent evt( type );
	DispatchEvent( &evt );
}
void CEntity::repath()
{
    CVector2D destination;
    if( m_orderQueue.empty() )
        return;

    while( !m_orderQueue.empty() &&
            ( ( m_orderQueue.front().m_type == CEntityOrder::ORDER_GOTO_COLLISION )
              || ( m_orderQueue.front().m_type == CEntityOrder::ORDER_GOTO_NOPATHING )
              || ( m_orderQueue.front().m_type == CEntityOrder::ORDER_GOTO_SMOOTHED ) ) )
    {
        destination = m_orderQueue.front().m_data[0].location;
        m_orderQueue.pop_front();
    }
    g_Pathfinder.requestPath( me, destination );
}

void CEntity::reorient()
{
    m_orientation = m_graphics_orientation;

    m_ahead.x = sin( m_orientation.Y );
    m_ahead.y = cos( m_orientation.Y );
    if( m_bounds->m_type == CBoundingObject::BOUND_OABB )
        ((CBoundingBox*)m_bounds)->setOrientation( m_ahead );
    updateActorTransforms();
}

void CEntity::teleport()
{
    m_position = m_graphics_position;
    m_bounds->setPosition( m_position.X, m_position.Z );
    updateCollisionPatch();
    repath();
}

void CEntity::playerChanged()
{
    if( m_actor )
        m_actor->SetPlayerID( m_player->GetPlayerID() );
}

void CEntity::checkSelection()
{
    if( m_selected )
    {
        if( !g_Selection.isSelected( me ) )
            g_Selection.addSelection( me );
    }
    else
    {
        if( g_Selection.isSelected( me ) )
            g_Selection.removeSelection( me );
    }
}

void CEntity::checkGroup()
{
    g_Selection.changeGroup( me, -1 ); // Ungroup
    if( ( m_grouped >= 0 ) && ( m_grouped < MAX_GROUPS ) )
        g_Selection.changeGroup( me, m_grouped );
}

void CEntity::interpolate( float relativeoffset )
{
    CVector3D old_graphics_position = m_graphics_position;
    CVector3D old_graphics_orientation = m_graphics_orientation;

    m_graphics_position = Interpolate<CVector3D>( m_position_previous, m_position, relativeoffset );

    // Avoid wraparound glitches for interpolating angles.
	
	m_orientation.X = fmodf(m_orientation.X, 2*PI); // (ensure the following loops can't take forever)
	m_orientation.Y = fmodf(m_orientation.Y, 2*PI);
	m_orientation.Z = fmodf(m_orientation.Z, 2*PI);

    while( m_orientation.Y < m_orientation_previous.Y - PI )
        m_orientation_previous.Y -= 2 * PI;
    while( m_orientation.Y > m_orientation_previous.Y + PI )
        m_orientation_previous.Y += 2 * PI;

	while( m_orientation.X < m_orientation_previous.X - PI )
        m_orientation_previous.X -= 2 * PI;
    while( m_orientation.X > m_orientation_previous.X + PI )
        m_orientation_previous.X += 2 * PI;

	while( m_orientation.Z < m_orientation_previous.Z - PI )
        m_orientation_previous.Z -= 2 * PI;
    while( m_orientation.Z > m_orientation_previous.Z + PI )
        m_orientation_previous.Z += 2 * PI;

    m_graphics_orientation = Interpolate<CVector3D>( m_orientation_previous, m_orientation, relativeoffset );

    // Mark the actor transform data as invalid if the entity has moved since
    // the last call to 'interpolate'.
    // position.Y is ignored because we can't determine the new value without
    // calling snapToGround, which is slow. TODO: This may need to be adjusted to
    // handle flying units or moving terrain.
    if( m_graphics_orientation != old_graphics_orientation ||
        m_graphics_position.X != old_graphics_position.X ||
        m_graphics_position.Z != old_graphics_position.Z		
		)
	{															
        m_actor_transform_valid = false;
	}
    // Update the actor transform data when necessary.
    if( !m_actor_transform_valid )
    {
        snapToGround();
        updateActorTransforms();
        m_actor_transform_valid = true;
    }
}

void CEntity::invalidateActor()
{
    m_actor_transform_valid = false;
}

void CEntity::render()
{
    if( !m_orderQueue.empty() )
    {
        std::deque<CEntityOrder>::iterator it;
        CBoundingObject* destinationCollisionObject;
        float x0, y0, x, y;

        x = m_orderQueue.front().m_data[0].location.x;
        y = m_orderQueue.front().m_data[0].location.y;

        for( it = m_orderQueue.begin(); it < m_orderQueue.end(); it++ )
        {
            if( it->m_type == CEntityOrder::ORDER_PATROL )
                break;
            x = it->m_data[0].location.x;
            y = it->m_data[0].location.y;
        }
        destinationCollisionObject = getContainingObject( CVector2D( x, y ) );

        glShadeModel( GL_FLAT );
        glBegin( GL_LINE_STRIP );

        glVertex3f( m_position.X, m_position.Y + 0.25f, m_position.Z );

        x = m_position.X;
        y = m_position.Z;

        for( it = m_orderQueue.begin(); it < m_orderQueue.end(); it++ )
        {
            x0 = x;
            y0 = y;
            x = it->m_data[0].location.x;
            y = it->m_data[0].location.y;
            rayIntersectionResults r;
            CVector2D fwd( x - x0, y - y0 );
            float l = fwd.length();
            fwd = fwd.normalize();
            CVector2D rgt = fwd.beta();
            if( getRayIntersection( CVector2D( x0, y0 ), fwd, rgt, l, m_bounds->m_radius, destinationCollisionObject, &r ) )
            {
                glEnd();
                glBegin( GL_LINES );
                glColor3f( 1.0f, 0.0f, 0.0f );
                glVertex3f( x0 + fwd.x * r.distance, getAnchorLevel( x0 + fwd.x * r.distance, y0 + fwd.y * r.distance ) + 0.25f, y0 + fwd.y * r.distance );
                glVertex3f( r.position.x, getAnchorLevel( r.position.x, r.position.y ) + 0.25f, r.position.y );
                glEnd();
                glBegin( GL_LINE_STRIP );
                glVertex3f( x0, getAnchorLevel( x0, y0 ), y0 );
            }
            switch( it->m_type )
            {
                case CEntityOrder::ORDER_GOTO:
                glColor3f( 1.0f, 0.0f, 0.0f );
                break;
                case CEntityOrder::ORDER_GOTO_COLLISION:
                glColor3f( 1.0f, 0.5f, 0.5f );
                break;
                case CEntityOrder::ORDER_GOTO_NOPATHING:
                case CEntityOrder::ORDER_GOTO_SMOOTHED:
                glColor3f( 0.5f, 0.5f, 0.5f );
                break;
                case CEntityOrder::ORDER_PATROL:
                glColor3f( 0.0f, 1.0f, 0.0f );
                break;
                default:
                continue;
            }

            glVertex3f( x, getAnchorLevel( x, y ) + 0.25f, y );
        }

        glEnd();
        glShadeModel( GL_SMOOTH );
    }

    glColor3f( 1.0f, 1.0f, 1.0f );
    if( getCollisionObject( this ) )
        glColor3f( 0.5f, 0.5f, 1.0f );
    m_bounds->render( getAnchorLevel( m_position.X, m_position.Z ) + 0.25f ); //m_position.Y + 0.25f );
}

float CEntity::getAnchorLevel( float x, float z )
{
    CTerrain *pTerrain = g_Game->GetWorld()->GetTerrain();
    float groundLevel = pTerrain->getExactGroundLevel( x, z );
    if( m_anchorType==L"Ground" )
    {
        return groundLevel;
    }
    else
    {
        return max( groundLevel, g_Renderer.GetWaterManager()->m_WaterHeight );
    }
}
int CEntity::findSector( int divs, float angle, float maxAngle, bool negative )
{
	float step=maxAngle/divs;
	if ( negative )
	{
		float tracker;
		int i=1, sectorRemainder;
		for ( tracker=-maxAngle/2.0f; tracker+step<0.0f; tracker+=step, ++i )
		{
			if ( angle > tracker && angle <= tracker+step ) 
				return i;
		}
		sectorRemainder = i;
		i=divs;
		for ( tracker=maxAngle/2.0f; tracker-step>0.0f; tracker-=step, --i )
		{
			if ( angle < tracker && angle >= tracker-step ) 
				return i;
		}
		return sectorRemainder;
	}
	else
	{
		int i=1;
		for (  float tracker=0.0f; tracker<maxAngle; tracker+=step, ++i )
		{
			if ( angle > tracker && angle <= tracker+step )
				return i;
		}
	}
	debug_warn("CEntity::findSector() - invalid parameters passed.");
	return -1;
}
void CEntity::renderSelectionOutline( float alpha )
{
    if( !m_bounds )
        return;

    if( getCollisionObject( this ) )
        glColor4f( 1.0f, 0.5f, 0.5f, alpha );
    else
    {
        const SPlayerColour& col = m_player->GetColour();
        glColor3f( col.r, col.g, col.b );
    }

    glBegin( GL_LINE_LOOP );

    CVector3D pos = m_graphics_position;

    switch( m_bounds->m_type )
    {
        case CBoundingObject::BOUND_CIRCLE:
        {
            float radius = ((CBoundingCircle*)m_bounds)->m_radius;
            for( int i = 0; i < SELECTION_CIRCLE_POINTS; i++ )
            {
                float ang = i * 2 * PI / (float)SELECTION_CIRCLE_POINTS;
                float x = pos.X + radius * sin( ang );
                float y = pos.Z + radius * cos( ang );
#ifdef SELECTION_TERRAIN_CONFORMANCE

                glVertex3f( x, getAnchorLevel( x, y ) + 0.25f, y );
#else

                glVertex3f( x, pos.Y + 0.25f, y );
#endif

            }
            break;
        }
        case CBoundingObject::BOUND_OABB:
        {
            CVector2D p, q;
            CVector2D u, v;
            q.x = pos.X;
            q.y = pos.Z;
            float d = ((CBoundingBox*)m_bounds)->m_d;
            float w = ((CBoundingBox*)m_bounds)->m_w;

            u.x = sin( m_graphics_orientation.Y );
            u.y = cos( m_graphics_orientation.Y );
            v.x = u.y;
            v.y = -u.x;

#ifdef SELECTION_TERRAIN_CONFORMANCE

            for( int i = SELECTION_BOX_POINTS; i > -SELECTION_BOX_POINTS; i-- )
            {
                p = q + u * d + v * ( w * (float)i / (float)SELECTION_BOX_POINTS );
                glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );
            }

            for( int i = SELECTION_BOX_POINTS; i > -SELECTION_BOX_POINTS; i-- )
            {
                p = q + u * ( d * (float)i / (float)SELECTION_BOX_POINTS ) - v * w;
                glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );
            }

            for( int i = -SELECTION_BOX_POINTS; i < SELECTION_BOX_POINTS; i++ )
            {
                p = q - u * d + v * ( w * (float)i / (float)SELECTION_BOX_POINTS );
                glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );
            }

            for( int i = -SELECTION_BOX_POINTS; i < SELECTION_BOX_POINTS; i++ )
            {
                p = q + u * ( d * (float)i / (float)SELECTION_BOX_POINTS ) + v * w;
                glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );
            }
#else
            p = q + u * h + v * w;
            glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );

            p = q + u * h - v * w;
            glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );

            p = q - u * h + v * w;
            glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );

            p = q + u * h + v * w;
            glVertex3f( p.x, getAnchorLevel( p.x, p.y ) + 0.25f, p.y );
#endif


            break;
        }
    }

    glEnd();
}
CVector2D CEntity::getScreenCoords( float height )
{
	CCamera &g_Camera=*g_Game->GetView()->GetCamera();

    float sx, sy;
    CVector3D above;
    above.X = m_position.X;
    above.Z = m_position.Z;
    above.Y = getAnchorLevel(m_position.X, m_position.Z) + height;
    g_Camera.GetScreenCoordinates(above, sx, sy);
	return CVector2D( sx, sy );
}
void CEntity::renderBarBorders()
{
	if ( m_staminaBarHeight >= 0 && 
		g_Selection.m_unitUITextures.find(m_healthBorderName) != g_Selection.m_unitUITextures.end() )
	{
		ogl_tex_bind( g_Selection.m_unitUITextures[m_healthBorderName] );
		CVector2D pos = getScreenCoords( m_healthBarHeight );

		float left = pos.x - m_healthBorderWidth/2;
		float right = pos.x + m_healthBorderWidth/2;
		pos.y = g_yres - pos.y;
		float bottom = pos.y + m_healthBorderHeight/2;
		float top = pos.y - m_healthBorderHeight/2;

		glBegin(GL_QUADS);

		glTexCoord2f(0.0f, 0.0f); glVertex3f( left, bottom, 0 );
		glTexCoord2f(0.0f, 1.0f); glVertex3f( left, top, 0 );
		glTexCoord2f(1.0f, 1.0f); glVertex3f( right, top, 0 );
		glTexCoord2f(1.0f, 0.0f); glVertex3f( right, bottom, 0 );

		glEnd();
	}
	if ( m_staminaBarHeight >= 0 && 
		g_Selection.m_unitUITextures.find(m_staminaBorderName) != g_Selection.m_unitUITextures.end() )
	{
		ogl_tex_bind( g_Selection.m_unitUITextures[m_staminaBorderName] );

		CVector2D pos = getScreenCoords( m_staminaBarHeight );
		float left = pos.x - m_staminaBorderWidth/2;
		float right = pos.x + m_staminaBorderWidth/2;
		pos.y = g_yres - pos.y;
		float bottom = pos.y + m_staminaBorderHeight/2;
		float top = pos.y - m_staminaBorderHeight/2;

		glBegin(GL_QUADS);

		glTexCoord2f(0.0f, 0.0f); glVertex3f( left, bottom, 0 );
		glTexCoord2f(0.0f, 1.0f); glVertex3f( left, top, 0 );
		glTexCoord2f(1.0f, 1.0f); glVertex3f( right, top, 0 );
		glTexCoord2f(1.0f, 0.0f); glVertex3f( right, bottom, 0 );

		glEnd();
	}
}
void CEntity::renderHealthBar()
{
    if( !m_bounds )
        return;
    if( m_healthBarHeight < 0 )
        return;  // negative bar height means don't display health bar

    float fraction;
	if(m_healthMax == 0) fraction = 1.0f;
	else fraction = clamp(m_healthCurr / m_healthMax, 0.0f, 1.0f);

	CVector2D pos = getScreenCoords( m_healthBarHeight );
    float x1 = pos.x - m_healthBarSize/2;
    float x2 = pos.x + m_healthBarSize/2;
    float y = g_yres - pos.y;

	glLineWidth( m_healthBarWidth );
	glBegin(GL_LINES);

    // green part of bar
    glColor3f( 0, 1, 0 );
    glVertex3f( x1, y, 0 );
    glColor3f( 0, 1, 0 );
    glVertex3f( x1 + m_healthBarSize*fraction, y, 0 );

    // red part of bar
    glColor3f( 1, 0, 0 );
    glVertex3f( x1 + m_healthBarSize*fraction, y, 0 );
    glColor3f( 1, 0, 0 );
    glVertex3f( x2, y, 0 );

    glEnd();

	glLineWidth(1.0f);

}

void CEntity::renderStaminaBar()
{
    if( !m_bounds )
        return;
    if( m_staminaBarHeight < 0 )
        return;  // negative bar height means don't display stamina bar

    float fraction;
	if(m_staminaMax == 0) fraction = 1.0f;
	else fraction = clamp(m_staminaCurr / m_staminaMax, 0.0f, 1.0f);

    CVector2D pos = getScreenCoords( m_staminaBarHeight );
    float x1 = pos.x - m_staminaBarSize/2;
    float x2 = pos.x + m_staminaBarSize/2;
    float y = g_yres - pos.y;

	glLineWidth( m_staminaBarWidth );
    glBegin(GL_LINES);

    // blue part of bar
    glColor3f( 0.1f, 0.1f, 1 );
    glVertex3f( x1, y, 0 );
    glColor3f( 0.1f, 0.1f, 1 );
    glVertex3f( x1 + m_staminaBarSize*fraction, y, 0 );

    // purple part of bar
    glColor3f( 0.3f, 0, 0.3f );
    glVertex3f( x1 + m_staminaBarSize*fraction, y, 0 );
    glColor3f( 0.3f, 0, 0.3f );
    glVertex3f( x2, y, 0 );

    glEnd();
	glLineWidth(1.0f);
}
void CEntity::renderRank()
{
	if( !m_bounds )
        return;
    if( m_rankHeight < 0 )
        return;  // negative height means don't display rank
	//Check for valid texture
	if( g_Selection.m_unitUITextures.find( m_rankName ) == g_Selection.m_unitUITextures.end() )
		return;

    CCamera *g_Camera=g_Game->GetView()->GetCamera();

    float sx, sy;
    CVector3D above;
    above.X = m_position.X;
    above.Z = m_position.Z;
    above.Y = getAnchorLevel(m_position.X, m_position.Z) + m_rankHeight;
    g_Camera->GetScreenCoordinates(above, sx, sy);
    int size = m_rankWidth/2;

	float x1 = sx + m_healthBarSize/2;
    float x2 = sx + m_healthBarSize/2 + 2*size;
    float y1 = g_yres - (sy - size);	//top
	float y2 = g_yres - (sy + size);	//bottom

	ogl_tex_bind(g_Selection.m_unitUITextures[m_rankName]);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE);
	glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, g_Renderer.m_Options.m_LodBias);

	glBegin(GL_QUADS);

    glTexCoord2f(1.0f, 0.0f); glVertex3f( x2, y2, 0 );
    glTexCoord2f(1.0f, 1.0f); glVertex3f( x2, y1, 0 );
    glTexCoord2f(0.0f, 1.0f); glVertex3f( x1, y1, 0 );
    glTexCoord2f(0.0f, 0.0f); glVertex3f( x1, y2, 0 );

    glEnd();
}
void CEntity::renderRallyPoint()
{
	if ( !m_hasRallyPoint || g_Selection.m_unitUITextures.find(m_rallyTexture) == 
							g_Selection.m_unitUITextures.end() )
	{
		return;
	}
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);

	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);	
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
	glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, g_Renderer.m_Options.m_LodBias);

	CSprite sprite;
	CTexture tex;
	tex.SetHandle( g_Selection.m_unitUITextures[m_rallyTexture] );
	sprite.SetTexture(&tex);
	CVector3D rally = m_rallyPoint;
	rally.Y += m_rallyHeight/2.f + .1f;
	sprite.SetTranslation(rally);
	sprite.Render();
}
void CEntity::CalculateRun(float timestep)
{
	if( m_staminaMax > 0 )
	{
		if ( m_isRunning && m_runDecayRate > 0 )
		{
			m_staminaCurr = max( 0.0f, m_staminaCurr - timestep / 1000.0f / m_runDecayRate * m_staminaMax );
		}
		else if ( m_orderQueue.empty() && m_runRegenRate > 0 )
		{
			m_staminaCurr = min( m_staminaMax, m_staminaCurr + timestep / 1000.0f / m_runRegenRate * m_staminaMax );
		}
	}
}

void CEntity::CalculateHealth(float timestep)
{
	if ( m_healthDecay && m_healthDecayRate > 0 )
	{
		m_healthCurr = max( 0.0f, m_healthCurr - timestep / 1000.0f / m_healthDecayRate * m_healthMax );
	}
	else if ( m_healthRegenRate > 0 && g_Game->GetTime() - m_lastCombatTime > m_healthRegenStart )
	{
		m_healthCurr = min( m_healthMax, m_healthCurr + timestep / 1000.0f / m_healthRegenRate * m_healthMax );
	}
}

/*

 Scripting interface

*/

// Scripting initialization

void CEntity::ScriptingInit()
{
    AddMethod<jsval, &CEntity::ToString>( "toString", 0 );
    AddMethod<bool, &CEntity::OrderSingle>( "order", 1 );
    AddMethod<bool, &CEntity::OrderQueued>( "orderQueued", 1 );
	AddMethod<jsval, &CEntity::TerminateOrder>( "terminateOrder", 1 );
    AddMethod<bool, &CEntity::Kill>( "kill", 0 );
    AddMethod<bool, &CEntity::IsIdle>( "isIdle", 0 );
    AddMethod<bool, &CEntity::HasClass>( "hasClass", 1 );
    AddMethod<jsval, &CEntity::GetSpawnPoint>( "getSpawnPoint", 1 );
    AddMethod<jsval, &CEntity::AddAura>( "addAura", 3 );
    AddMethod<jsval, &CEntity::RemoveAura>( "removeAura", 1 );
    AddMethod<jsval, &CEntity::SetActionParams>( "setActionParams", 5 );
	AddMethod<int, &CEntity::GetCurrentRequest>( "getCurrentRequest", 0 );
	AddMethod<bool, &CEntity::ForceCheckListeners>( "forceCheckListeners", 2 );
	AddMethod<bool, &CEntity::RequestNotification>( "requestNotification", 4 );
	AddMethod<jsval, &CEntity::DestroyNotifier>( "destroyNotifier", 1 );
	AddMethod<jsval, &CEntity::DestroyAllNotifiers>( "destroyAllNotifiers", 0 );
	AddMethod<jsval, &CEntity::TriggerRun>( "triggerRun", 1 );
	AddMethod<jsval, &CEntity::SetRun>( "setRun", 1 );
	AddMethod<jsval, &CEntity::GetRunState>( "getRunState", 0 );
	AddMethod<bool, &CEntity::IsInFormation>( "isInFormation", 0 );
	AddMethod<jsval, &CEntity::GetFormationBonus>( "getFormationBonus", 0 );
	AddMethod<jsval, &CEntity::GetFormationBonusType>( "getFormationBonusType", 0 );
	AddMethod<jsval, &CEntity::GetFormationBonusVal>( "getFormationBonusVal", 0 );
	AddMethod<jsval, &CEntity::GetFormationPenalty>( "getFormationPenalty", 0 );
	AddMethod<jsval, &CEntity::GetFormationPenaltyType>( "getFormationPenaltyType", 0 );
	AddMethod<jsval, &CEntity::GetFormationPenaltyVal>( "getFormationPenaltyVal", 0 );
	AddMethod<jsval, &CEntity::RegisterDamage>( "registerDamage", 0 );
	AddMethod<jsval, &CEntity::RegisterOrderChange>( "registerOrderChange", 0 );
	AddMethod<jsval, &CEntity::GetAttackDirections>( "getAttackDirections", 0 );
	AddMethod<jsval, &CEntity::FindSector>("findSector", 4);
	AddMethod<jsval, &CEntity::GetHeight>("getHeight", 0 );
	AddMethod<jsval, &CEntity::HasRallyPoint>("hasRallyPoint", 0 );
	AddMethod<jsval, &CEntity::SetRallyPoint>("setRallyPoint", 0 );
	AddMethod<jsval, &CEntity::GetRallyPoint>("getRallyPoint", 0 );

    AddClassProperty( L"template", (CBaseEntity* CEntity::*)&CEntity::m_base, false, (NotifyFn)&CEntity::loadBase );
    AddClassProperty( L"traits.id.classes", (GetFn)&CEntity::getClassSet, (SetFn)&CEntity::setClassSet );

    CJSComplex<CEntity>::ScriptingInit( "Entity", Construct, 2 );
}

// Script constructor

JSBool CEntity::Construct( JSContext* cx, JSObject* UNUSED(obj), uint argc, jsval* argv, jsval* rval )
{
    debug_assert( argc >= 2 );

    CVector3D position;
    float orientation = (float)( PI * ( (double)( rand() & 0x7fff ) / (double)0x4000 ) );

    JSObject* jsBaseEntity = JSVAL_TO_OBJECT( argv[0] );
    CStrW templateName;

	CPlayer* player = g_Game->GetPlayer( 0 );

    CBaseEntity* baseEntity = NULL;
    if( JSVAL_IS_OBJECT( argv[0] ) ) // only set baseEntity if jsBaseEntity is a valid object
        baseEntity = ToNative<CBaseEntity>( cx, jsBaseEntity );

    if( !baseEntity )
    {
        try
        {
            templateName = g_ScriptingHost.ValueToUCString( argv[0] );
        }
        catch( PSERROR_Scripting_ConversionFailed )
        {
            *rval = JSVAL_NULL;
            JS_ReportError( cx, "Invalid template identifier" );
            return( JS_TRUE );
        }
        baseEntity = g_EntityTemplateCollection.getTemplate( templateName );
    }

    if( !baseEntity )
    {
        *rval = JSVAL_NULL;
        JS_ReportError( cx, "No such template: %s", CStr8(templateName).c_str() );
        return( JS_TRUE );
    }

    JSI_Vector3D::Vector3D_Info* jsVector3D = NULL;
    if( JSVAL_IS_OBJECT( argv[1] ) )
        jsVector3D = (JSI_Vector3D::Vector3D_Info*)JS_GetInstancePrivate( cx, JSVAL_TO_OBJECT( argv[1] ), &JSI_Vector3D::JSI_class, NULL );

    if( jsVector3D )
    {
        position = *( jsVector3D->vector );
    }

    if( argc >= 3 )
    {
        try
        {
            orientation = ToPrimitive<float>( argv[2] );
        }
        catch( PSERROR_Scripting_ConversionFailed )
        {
            // TODO: Net-safe random for this parameter.
            orientation = 0.0f;
        }
    }

    if( argc >= 4 )
    {
        try
        {
            player = ToPrimitive<CPlayer*>( argv[3] );
        }
        catch( PSERROR_Scripting_ConversionFailed )
        {
			player = g_Game->GetPlayer( 0 );
        }
    }

	std::set<CStrW> selections; // TODO: let scripts specify selections?
    HEntity handle = g_EntityManager.create( baseEntity, position, orientation, selections );
	handle->SetPlayer( player );
	handle->Initialize();

    *rval = ToJSVal<CEntity>( *handle );
    return( JS_TRUE );
}

// Script-bound methods

jsval CEntity::ToString( JSContext* cx, uintN UNUSED(argc), jsval* UNUSED(argv) )
{
    wchar_t buffer[256];
    swprintf( buffer, 256, L"[object Entity: %ls]", m_base->m_Tag.c_str() );
    buffer[255] = 0;
    utf16string str16(buffer, buffer+wcslen(buffer));
    return( STRING_TO_JSVAL( JS_NewUCStringCopyZ( cx, str16.c_str() ) ) );
}

bool CEntity::Order( JSContext* cx, uintN argc, jsval* argv, bool Queued )
{
    // This needs to be sorted (uses Scheduler rather than network messaging)
    debug_assert( argc >= 1 );

    int orderCode;

    try
    {
        orderCode = ToPrimitive<int>( argv[0] );
    }
    catch( PSERROR_Scripting_ConversionFailed )
    {
        JS_ReportError( cx, "Invalid order type" );
        return( false );
    }

    CEntityOrder newOrder;
	CEntity* target;

    (int&)newOrder.m_type = orderCode;

    switch( orderCode )
    {
        case CEntityOrder::ORDER_GOTO:
		case CEntityOrder::ORDER_RUN:
        case CEntityOrder::ORDER_PATROL:
			if( argc < 3 )
			{
				JS_ReportError( cx, "Too few parameters" );
				return( false );
			}
			try
			{
				newOrder.m_data[0].location.x = ToPrimitive<float>( argv[1] );
				newOrder.m_data[0].location.y = ToPrimitive<float>( argv[2] );
			}
			catch( PSERROR_Scripting_ConversionFailed )
			{
				JS_ReportError( cx, "Invalid location" );
				return( false );
			}
			if ( orderCode == CEntityOrder::ORDER_RUN )
				m_triggerRun = true;
			//It's not a notification order
			if ( argc == 3 )
			{
				if ( m_destroyNotifiers )
				{
					m_currentRequest=0;
					DestroyAllNotifiers();
				}
			}
			break;
        case CEntityOrder::ORDER_GENERIC:
			if( argc < 3 )
			{
				JS_ReportError( cx, "Too few parameters" );
				return( false );
			}
			target = ToNative<CEntity>( argv[1] );
			if( !target )
			{
				JS_ReportError( cx, "Invalid target" );
				return( false );
			}
			newOrder.m_data[0].entity = target->me;
			try
			{
				newOrder.m_data[1].data = ToPrimitive<int>( argv[2] );
			}
			catch( PSERROR_Scripting_ConversionFailed )
			{
				JS_ReportError( cx, "Invalid generic order type" );
				return( false );
			}
			//It's not a notification order
			if ( argc == 3 )
			{
				if ( m_destroyNotifiers )
				{
					m_currentRequest=0;
					DestroyAllNotifiers();
				}
			}
			break;
        case CEntityOrder::ORDER_PRODUCE:
			if( argc < 3 )
			{
				JS_ReportError( cx, "Too few parameters" );
				return( false );
			}
			try {
				newOrder.m_data[0].string = ToPrimitive<CStrW>(argv[2]);
				newOrder.m_data[1].data = ToPrimitive<int>(argv[1]);
			}
			catch( PSERROR_Scripting_ConversionFailed )
			{
				JS_ReportError( cx, "Invalid parameter types" );
				return( false );
			}
			break;
        default:
			JS_ReportError( cx, "Invalid order type" );
			return( false );
    }

    if( !Queued )
        clearOrders();
    pushOrder( newOrder );

    return( true );
}

bool CEntity::Kill( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
    CEventDeath evt;
    DispatchEvent( &evt );

    for( AuraTable::iterator it = m_auras.begin(); it != m_auras.end(); it++ )
    {
        it->second->RemoveAll();
        delete it->second;
    }
    m_auras.clear();

    for( AuraSet::iterator it = m_aurasInfluencingMe.begin(); it != m_aurasInfluencingMe.end(); it++ )
    {
        (*it)->Remove( this );
    }
    m_aurasInfluencingMe.clear();

    if( m_bounds )
    {
        delete( m_bounds );
        m_bounds = NULL;
    }

    if( m_extant )
    {
        m_extant = false;
    }

    updateCollisionPatch();

    g_Selection.removeAll( me );

    clearOrders();

	g_EntityManager.SetDeath(true);


    if( m_actor )
	{
		m_actor->SetEntitySelection( L"death" );       
		m_actor->SetRandomAnimation( "death", true );
	}

    return( true );
}

jsval CEntity::GetSpawnPoint( JSContext* UNUSED(cx), uintN argc, jsval* argv )
{
    float spawn_clearance = 2.0f;
    if( argc >= 1 )
    {
        CBaseEntity* be = ToNative<CBaseEntity>( argv[0] );
        if( be )
        {
            switch( be->m_bound_type )
            {
                case CBoundingObject::BOUND_CIRCLE:
                spawn_clearance = be->m_bound_circle->m_radius;
                break;
                case CBoundingObject::BOUND_OABB:
                spawn_clearance = be->m_bound_box->m_radius;
                break;
                default:
                debug_warn("No bounding information for spawned object!" );
            }
        }
        else
            spawn_clearance = ToPrimitive<float>( argv[0] );
    }
    else
        debug_warn("No arguments to Entity::GetSpawnPoint()" );

    // TODO: Make netsafe.
    CBoundingCircle spawn( 0.0f, 0.0f, spawn_clearance, 0.0f );

    if( m_bounds->m_type == CBoundingObject::BOUND_OABB )
    {
        CBoundingBox* oabb = (CBoundingBox*)m_bounds;

        // Pick a start point

        int edge = rand() & 3;
        int point;

        double max_w = oabb->m_w + spawn_clearance + 1.0;
        double max_d = oabb->m_d + spawn_clearance + 1.0;
        int w_count = (int)( max_w * 2 );
        int d_count = (int)( max_d * 2 );

        CVector2D w_step = oabb->m_v * (float)( max_w / w_count );
        CVector2D d_step = oabb->m_u * (float)( max_d / d_count );
        CVector2D pos( m_position );
        if( edge & 1 )
        {
            point = rand() % ( 2 * d_count ) - d_count;
            pos += ( oabb->m_v * (float)max_w + d_step * (float)point ) * ( ( edge & 2 ) ? -1.0f : 1.0f );
        }
        else
        {
            point = rand() % ( 2 * w_count ) - w_count;
            pos += ( oabb->m_u * (float)max_d + w_step * (float)point ) * ( ( edge & 2 ) ? -1.0f : 1.0f );
        }

        int start_edge = edge;
        int start_point = point;

        spawn.m_pos = pos;

        // Then step around the edge (clockwise) until a free space is found, or
        // we've gone all the way around.
        while( getCollisionObject( &spawn ) )
        {
            switch( edge )
            {
                case 0:
                point++;
                pos += w_step;
                if( point >= w_count )
                {
                    edge = 1;
                    point = -d_count;
                }
                break;
                case 1:
                point++;
                pos -= d_step;
                if( point >= d_count )
                {
                    edge = 2;
                    point = w_count;
                }
                break;
                case 2:
                point--;
                pos -= w_step;
                if( point <= -w_count )
                {
                    edge = 3;
                    point = d_count;
                }
                break;
                case 3:
                point--;
                pos += d_step;
                if( point <= -d_count )
                {
                    edge = 0;
                    point = -w_count;
                }
                break;
            }
            if( ( point == start_point ) && ( edge == start_edge ) )
                return( JSVAL_NULL );
            spawn.m_pos = pos;
        }
        CVector3D rval( pos.x, getAnchorLevel( pos.x, pos.y ), pos.y );
        return( ToJSVal( rval ) );
    }
    else if( m_bounds->m_type == CBoundingObject::BOUND_CIRCLE )
    {
        float ang;
        ang = (float)( rand() & 0x7fff ) / (float)0x4000; /* 0...2 */
        ang *= PI;
        float radius = m_bounds->m_radius + 1.0f + spawn_clearance;
        float d_ang = spawn_clearance / ( 2.0f * radius );
        float ang_end = ang + 2.0f * PI;
        float x = 0.0f, y = 0.0f; // make sure they're initialized
        for( ; ang < ang_end; ang += d_ang )
        {
            x = m_position.X + radius * cos( ang );
            y = m_position.Z + radius * sin( ang );
            spawn.setPosition( x, y );
            if( !getCollisionObject( &spawn ) )
                break;
        }
        if( ang < ang_end )
        {
            // Found a satisfactory position...
            CVector3D pos( x, 0, y );
            pos.Y = getAnchorLevel( x, y );
            return( ToJSVal( pos ) );
        }
        else
            return( JSVAL_NULL );
    }
    return( JSVAL_NULL );
}

jsval CEntity::AddAura( JSContext* cx, uintN argc, jsval* argv )
{
    debug_assert( argc >= 4 );
    debug_assert( JSVAL_IS_OBJECT(argv[3]) );

    CStrW name = ToPrimitive<CStrW>( argv[0] );
    float radius = ToPrimitive<float>( argv[1] );
	size_t tickRate = max( 0, ToPrimitive<int>( argv[2] ) );	// since it's a size_t we don't want it to be negative
    JSObject* handler = JSVAL_TO_OBJECT( argv[3] );

    if( m_auras[name] )
    {
        delete m_auras[name];
    }
    m_auras[name] = new CAura( cx, this, name, radius, tickRate, handler );

    return JSVAL_VOID;
}

jsval CEntity::RemoveAura( JSContext* UNUSED(cx), uintN argc, jsval* argv )
{
    debug_assert( argc >= 1 );
    CStrW name = ToPrimitive<CStrW>( argv[0] );
    if( m_auras[name] )
    {
        delete m_auras[name];
        m_auras.erase(name);
    }
    return JSVAL_VOID;
}

jsval CEntity::SetActionParams( JSContext* UNUSED(cx), uintN argc, jsval* argv )
{
    debug_assert( argc == 5 );

    int id = ToPrimitive<int>( argv[0] );
    float minRange = ToPrimitive<int>( argv[1] );
    float maxRange = ToPrimitive<int>( argv[2] );
    uint speed = ToPrimitive<uint>( argv[3] );
    CStr8 animation = ToPrimitive<CStr8>( argv[4] );

	m_actions[id] = SEntityAction( minRange, maxRange, speed, animation );

    return JSVAL_VOID;
}

bool CEntity::RequestNotification( JSContext* cx, uintN argc, jsval* argv )
{
	if( argc < 4 )
	{
		JS_ReportError( cx, "Too few parameters" );
		return( false );
	}

	CEntityListener notify;
	CEntity *target = ToNative<CEntity>( argv[0] );
	(int&)notify.m_type = ToPrimitive<int>( argv[1] );
	bool tmpDestroyNotifiers = ToPrimitive<bool>( argv[2] );
	m_destroyNotifiers = !ToPrimitive<bool>( argv[3] );

	if (target == this)
		return false;

	notify.m_sender = this;

	//Clean up old requests
	if ( tmpDestroyNotifiers )
		DestroyAllNotifiers();
	//If new request is not the same and we're destroy notifiers, reset
	else if ( !(notify.m_type & m_currentRequest) && m_destroyNotifiers )
		DestroyAllNotifiers();

	m_currentRequest = notify.m_type;
	m_notifiers.push_back( target );
	int result = target->m_currentNotification & notify.m_type;

	//If our target isn't stationary and it's doing something we want to follow, send notification
	if ( result && !target->m_orderQueue.empty() )
	{
		CEntityOrder order = target->m_orderQueue.front();
		switch( result )
		{
			 case CEntityListener::NOTIFY_GOTO:
			 case CEntityListener::NOTIFY_RUN:
				DispatchNotification( order, result );
				break;

			 case CEntityListener::NOTIFY_HEAL:
			 case CEntityListener::NOTIFY_ATTACK:
			 case CEntityListener::NOTIFY_GATHER:
		     case CEntityListener::NOTIFY_DAMAGE:
				 DispatchNotification( order, result );
				 break;
			 default:
				 JS_ReportError( cx, "Invalid order type" );
				 break;
		}
		target->m_listeners.push_back( notify );
		return true;
	}

	target->m_listeners.push_back( notify );
	return false;
}
int CEntity::GetCurrentRequest( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return m_currentRequest;
}
bool CEntity::ForceCheckListeners( JSContext *cx, uintN argc, jsval* argv )
{
	if( argc < 2 )
	{
		JS_ReportError( cx, "Too few parameters" );
		return false;
	}
	int type = ToPrimitive<int>( argv[0] );	   //notify code
	m_currentNotification = type;

	CEntity *target = ToNative<CEntity>( argv[1] );
	if ( target->m_orderQueue.empty() )
		return false;

	CEntityOrder order = target->m_orderQueue.front();
	for (size_t i=0; i<m_listeners.size(); i++)
	{
		int result = m_listeners[i].m_type & type;
		if ( result )
		{
			 switch( result )
			 {
				 case CEntityListener::NOTIFY_GOTO:
				 case CEntityListener::NOTIFY_RUN:
				 case CEntityListener::NOTIFY_HEAL:
				 case CEntityListener::NOTIFY_ATTACK:
				 case CEntityListener::NOTIFY_GATHER:
				 case CEntityListener::NOTIFY_DAMAGE:
				 case CEntityListener::NOTIFY_IDLE:	//target should be 'this'
					m_listeners[i].m_sender->DispatchNotification( order, result );
					break;

				default:
					JS_ReportError( cx, "Invalid order type" );
					break;
			 }
		 }
	}
	return true;
}
void CEntity::CheckListeners( int type, CEntity *target)
{
	m_currentNotification = type;

	debug_assert(target);
	if ( target->m_orderQueue.empty() )
		return;

	CEntityOrder order = target->m_orderQueue.front();
	for (size_t i=0; i<m_listeners.size(); i++)
	{
		int result = m_listeners[i].m_type & type;
		if ( result )
		{
			 switch( result )
			 {
				 case CEntityListener::NOTIFY_GOTO:
				 case CEntityListener::NOTIFY_RUN:
				 case CEntityListener::NOTIFY_HEAL:
				 case CEntityListener::NOTIFY_ATTACK:
				 case CEntityListener::NOTIFY_GATHER:
				 case CEntityListener::NOTIFY_DAMAGE:
					 m_listeners[i].m_sender->DispatchNotification( order, result );
					 break;
				 default:
					debug_warn("Invalid notification: CheckListeners()");
					continue;
			 }
		}
	}
}
jsval CEntity::DestroyAllNotifiers( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	DestroyAllNotifiers();
	return JS_TRUE;
}
jsval CEntity::DestroyNotifier( JSContext* cx, uintN argc, jsval* argv )
{
	if ( argc < 1 )
	{
		JS_ReportError(cx, "too few parameters: CEntity::DestroyNotifier");
		return JS_FALSE;
	}
	DestroyNotifier( ToNative<CEntity>( argv[0] ) );
	return JS_TRUE;
}

jsval CEntity::TriggerRun( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	m_triggerRun = true;
	return JSVAL_VOID;
}

jsval CEntity::SetRun( JSContext* cx, uintN argc, jsval* argv )
{
	if( argc < 1 )
	{
		JS_ReportError( cx, "Too few parameters" );
		return( false );
	}
	m_shouldRun = ToPrimitive<bool> ( argv[0] );
	m_isRunning = m_shouldRun;
	return JSVAL_VOID;
}
jsval CEntity::GetRunState( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return BOOLEAN_TO_JSVAL( m_shouldRun );
}
jsval CEntity::GetFormationPenalty( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetPenalty() );
}
jsval CEntity::GetFormationPenaltyBase( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetPenaltyBase() );
}
jsval CEntity::GetFormationPenaltyType( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetPenaltyType() );
}
jsval CEntity::GetFormationPenaltyVal( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetPenaltyVal() );
}
jsval CEntity::GetFormationBonus( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetBonus() );
}
jsval CEntity::GetFormationBonusBase( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetBonusBase() );
}
jsval CEntity::GetFormationBonusType( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetBonusType() );
}
jsval CEntity::GetFormationBonusVal( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( GetFormation()->GetBase()->GetBonusVal() );
}

jsval CEntity::RegisterDamage( JSContext* cx, uintN argc, jsval* argv )
{
	if ( argc < 1 )
	{
		JS_ReportError( cx, "Too few parameters" );
		return( false );
	}
	CEntity* inflictor = ToNative<CEntity>( argv[0] );
	CVector2D up(1.0f, 0.0f);
	CVector2D pos = CVector2D( inflictor->m_position.X, inflictor->m_position.Z );
	CVector2D posDelta = (pos - m_position).normalize();

	float angle = acosf( up.dot(posDelta) );
	//Find what section it is between and "activate" it
	int sector = findSector(m_sectorDivs, angle, DEGTORAD(360.0f))-1;
	m_sectorValues[sector]=true;
	return JS_TRUE;
}
jsval CEntity::RegisterOrderChange( JSContext* cx, uintN argc, jsval* argv )
{
	if ( argc < 1 )
	{
		JS_ReportError( cx, "Too few parameters" );
		return( false );
	}
	CEntity* idleEntity = ToNative<CEntity>( argv[0] );

	CVector2D up(1.0f, 0.0f);
	CVector2D pos = CVector2D( idleEntity->m_position.X, idleEntity->m_position.Z );
	CVector2D posDelta = (pos - m_position).normalize();

	float angle = acosf( up.dot(posDelta) );
	//Find what section it is between and "deactivate" it
	int sector = MAX( 0.0, findSector(m_sectorDivs, angle, DEGTORAD(360.0f)) );
	m_sectorValues[sector]=false;
	return JS_TRUE;
}
jsval CEntity::GetAttackDirections( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	int directions=0;

	for ( std::vector<bool>::iterator it=m_sectorValues.begin(); it != m_sectorValues.end(); it++ )
	{
		if ( *it )
			++directions;
	}
	return ToJSVal( directions );
}
jsval CEntity::FindSector( JSContext* cx, uintN argc, jsval* argv )
{
	if ( argc < 4 )
	{
		JS_ReportError( cx, "Too few parameters" );
		return( false );
	}
	int divs = ToPrimitive<int>( argv[0] );
	float angle = ToPrimitive<float>( argv[1] );
	float maxAngle = ToPrimitive<float>( argv[2] );
	bool negative = ToPrimitive<bool>( argv[3] );

	return ToJSVal( findSector(divs, angle, maxAngle, negative) );
}
jsval CEntity::HasRallyPoint( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( m_hasRallyPoint );
}
jsval CEntity::GetRallyPoint( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	return ToJSVal( m_rallyPoint );
}
jsval CEntity::SetRallyPoint( JSContext* UNUSED(cx), uintN UNUSED(argc), jsval* UNUSED(argv) )
{
	m_hasRallyPoint = true;
	m_rallyPoint = g_Game->GetView()->GetCamera()->GetWorldCoordinates();
	return JS_TRUE;
}

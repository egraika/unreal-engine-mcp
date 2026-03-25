"""
Unreal Engine Advanced MCP Server

A streamlined MCP server focused on advanced composition tools for Unreal Engine.
Contains only the advanced tools from the expanded MCP tool system to keep tool count manageable.
"""

import logging
import socket
import json
import math
import struct
import time
import threading
from contextlib import asynccontextmanager
from typing import AsyncIterator, Dict, Any, Optional, List, Union
from mcp.server.fastmcp import FastMCP

from helpers.infrastructure_creation import (
    _create_street_grid, _create_street_lights, _create_town_vehicles, _create_town_decorations,
    _create_traffic_lights, _create_street_signage, _create_sidewalks_crosswalks, _create_urban_furniture,
    _create_street_utilities, _create_central_plaza
)
from helpers.building_creation import _create_town_building
from helpers.castle_creation import (
    get_castle_size_params, calculate_scaled_dimensions, build_outer_bailey_walls, 
    build_inner_bailey_walls, build_gate_complex, build_corner_towers, 
    build_inner_corner_towers, build_intermediate_towers, build_central_keep, 
    build_courtyard_complex, build_bailey_annexes, build_siege_weapons, 
    build_village_settlement, build_drawbridge_and_moat, add_decorative_flags
)
from helpers.house_construction import build_house

from helpers.mansion_creation import (
    get_mansion_size_params, calculate_mansion_layout, build_mansion_main_structure,
    build_mansion_exterior, add_mansion_interior
)
from helpers.actor_utilities import spawn_blueprint_actor, get_blueprint_material_info
from helpers.actor_name_manager import (
    safe_spawn_actor, safe_delete_actor
)
from helpers.bridge_aqueduct_creation import (
    build_suspension_bridge_structure, build_aqueduct_structure
)
from helpers.medieval_town_creation import create_kenshi_settlement as _create_kenshi_settlement

# ============================================================================
# Blueprint Node Graph Tools
# ============================================================================
from helpers.blueprint_graph import node_manager
from helpers.blueprint_graph import variable_manager
from helpers.blueprint_graph import connector_manager
from helpers.blueprint_graph import event_manager
from helpers.blueprint_graph import node_deleter
from helpers.blueprint_graph import node_properties
from helpers.blueprint_graph import function_manager
from helpers.blueprint_graph import function_io


# Configure logging with more detailed format
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s',
    handlers=[
        logging.FileHandler('unreal_mcp_advanced.log'),
    ]
)
logger = logging.getLogger("UnrealMCP_Advanced")

# Configuration
UNREAL_HOST = "127.0.0.1"
UNREAL_PORT = 55557

class UnrealConnection:
    """
    Robust connection to Unreal Engine with automatic retry and reconnection.
    
    Features:
    - Exponential backoff retry for connection attempts
    - Automatic reconnection on failure
    - Configurable timeouts per command type
    - Thread-safe operations
    - Detailed logging for debugging
    """
    
    # Configuration constants
    MAX_RETRIES = 3
    BASE_RETRY_DELAY = 0.5  # seconds
    MAX_RETRY_DELAY = 5.0   # seconds
    CONNECT_TIMEOUT = 10    # seconds
    DEFAULT_RECV_TIMEOUT = 30  # seconds
    LARGE_OP_RECV_TIMEOUT = 300  # seconds for large operations
    BUFFER_SIZE = 8192
    
    # Commands that need longer timeouts
    LARGE_OPERATION_COMMANDS = {
        "get_available_materials",
        "get_material_instance_parameters",
        "set_material_instance_parameters",
        "create_town",
        "create_castle_fortress",
        "construct_mansion",
        "create_suspension_bridge",
        "create_aqueduct",
        "create_maze",
        "create_kenshi_settlement",
        "generate_thumbnails",
        "scan_assets_for_thumbnails",
        "get_material_expressions",
        "get_material_connections",
        "get_material_function_info",
        "get_material_stats"
    }
    
    def __init__(self):
        """Initialize the connection."""
        self.socket = None
        self.connected = False
        self._lock = threading.RLock()  # RLock allows reentrant acquisition for retry logic
        self._last_error = None
    
    def _create_socket(self) -> socket.socket:
        """Create and configure a new socket."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(self.CONNECT_TIMEOUT)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 131072)  # 128KB
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 131072)  # 128KB
        
        # Set linger to ensure clean socket closure (l_onoff=1, l_linger=0)
        # struct linger is two 16-bit integers: l_onoff and l_linger
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('hh', 1, 0))
        except OSError:
            pass
        
        return sock
    
    def connect(self) -> bool:
        """
        Connect to Unreal Engine with retry logic.
        
        Uses exponential backoff for retries. Sleep occurs outside the lock
        to avoid blocking other threads during retry delays.
            
        Returns:
            True if connected successfully, False otherwise
        """
        for attempt in range(self.MAX_RETRIES + 1):
            # Hold lock only during connection attempt, not during sleep
            with self._lock:
                # Clean up any existing connection
                self._close_socket_unsafe()
                
                try:
                    logger.info(f"Connecting to Unreal at {UNREAL_HOST}:{UNREAL_PORT} (attempt {attempt + 1}/{self.MAX_RETRIES + 1})...")
                    
                    self.socket = self._create_socket()
                    self.socket.connect((UNREAL_HOST, UNREAL_PORT))
                    self.connected = True
                    self._last_error = None
                    
                    logger.info("Successfully connected to Unreal Engine")
                    return True
                    
                except socket.timeout as e:
                    self._last_error = f"Connection timeout: {e}"
                    logger.warning(f"Connection timeout (attempt {attempt + 1})")
                except ConnectionRefusedError as e:
                    self._last_error = f"Connection refused: {e}"
                    logger.warning(f"Connection refused - is Unreal Engine running? (attempt {attempt + 1})")
                except OSError as e:
                    self._last_error = f"OS error: {e}"
                    logger.warning(f"OS error during connection: {e} (attempt {attempt + 1})")
                except Exception as e:
                    self._last_error = f"Unexpected error: {e}"
                    logger.error(f"Unexpected connection error: {e} (attempt {attempt + 1})")
                
                self._close_socket_unsafe()
                self.connected = False
            
            # Sleep OUTSIDE the lock to allow other threads to proceed
            if attempt < self.MAX_RETRIES:
                delay = min(self.BASE_RETRY_DELAY * (2 ** attempt), self.MAX_RETRY_DELAY)
                logger.info(f"Retrying connection in {delay:.1f}s...")
                time.sleep(delay)
        
        logger.error(f"Failed to connect after {self.MAX_RETRIES + 1} attempts. Last error: {self._last_error}")
        return False
    
    def _close_socket_unsafe(self):
        """Close socket without lock (internal use only)."""
        if self.socket:
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except:
                pass
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
        self.connected = False
    
    def disconnect(self):
        """Safely disconnect from Unreal Engine."""
        with self._lock:
            self._close_socket_unsafe()
            logger.debug("Disconnected from Unreal Engine")

    def _get_timeout_for_command(self, command_type: str) -> int:
        """Get appropriate timeout for command type."""
        if any(large_cmd in command_type for large_cmd in self.LARGE_OPERATION_COMMANDS):
            return self.LARGE_OP_RECV_TIMEOUT
        return self.DEFAULT_RECV_TIMEOUT

    def _receive_response(self, command_type: str) -> bytes:
        """
        Receive complete JSON response from Unreal.
        
        Args:
            command_type: Type of command (used for timeout selection)
            
        Returns:
            Raw response bytes
            
        Raises:
            Exception: On timeout or connection error
        """
        timeout = self._get_timeout_for_command(command_type)
        self.socket.settimeout(timeout)
        
        chunks = []
        total_bytes = 0
        start_time = time.time()
        
        try:
            while True:
                # Check for overall timeout
                elapsed = time.time() - start_time
                if elapsed > timeout:
                    raise socket.timeout(f"Overall timeout after {elapsed:.1f}s")
                
                try:
                    chunk = self.socket.recv(self.BUFFER_SIZE)
                except socket.timeout:
                    # Check if we have a complete response
                    if chunks:
                        data = b''.join(chunks)
                        try:
                            json.loads(data.decode('utf-8'))
                            logger.info(f"Got complete response after recv timeout ({total_bytes} bytes)")
                            return data
                        except json.JSONDecodeError:
                            pass
                    raise
                
                if not chunk:
                    # Connection closed by remote
                    if not chunks:
                        raise ConnectionError("Connection closed before receiving any data")
                    break
                
                chunks.append(chunk)
                total_bytes += len(chunk)
                
                # Try to parse accumulated data as JSON
                data = b''.join(chunks)
                try:
                    decoded = data.decode('utf-8')
                    json.loads(decoded)
                    # Successfully parsed - we have complete response
                    logger.info(f"Received complete response ({total_bytes} bytes) for {command_type}")
                    return data
                except json.JSONDecodeError:
                    # Incomplete JSON, continue reading
                    continue
                except UnicodeDecodeError:
                    # Incomplete UTF-8, continue reading
                    continue
                    
        except socket.timeout:
            elapsed = time.time() - start_time
            if chunks:
                data = b''.join(chunks)
                try:
                    json.loads(data.decode('utf-8'))
                    logger.warning(f"Using response received before timeout ({total_bytes} bytes)")
                    return data
                except:
                    pass
            raise TimeoutError(f"Timeout after {elapsed:.1f}s waiting for response to {command_type} (received {total_bytes} bytes)")
        
        # If we get here, connection was closed
        if chunks:
            data = b''.join(chunks)
            try:
                json.loads(data.decode('utf-8'))
                return data
            except:
                raise ConnectionError(f"Connection closed with incomplete data ({total_bytes} bytes)")
        
        raise ConnectionError("Connection closed without response")

    def send_command(self, command: str, params: Dict[str, Any] = None) -> Optional[Dict[str, Any]]:
        """
        Send a command to Unreal Engine with automatic retry.
        
        Args:
            command: Command type string
            params: Command parameters dictionary
            
        Returns:
            Response dictionary or error dictionary
        """
        last_error = None
        
        for attempt in range(self.MAX_RETRIES + 1):
            try:
                return self._send_command_once(command, params, attempt)
            except (ConnectionError, TimeoutError, socket.error, OSError) as e:
                last_error = str(e)
                logger.warning(f"Command failed (attempt {attempt + 1}/{self.MAX_RETRIES + 1}): {e}")
                
                # Clean up and prepare for retry
                self.disconnect()
                
                if attempt < self.MAX_RETRIES:
                    delay = min(self.BASE_RETRY_DELAY * (2 ** attempt), self.MAX_RETRY_DELAY)
                    logger.info(f"Retrying command in {delay:.1f}s...")
                    time.sleep(delay)
            except Exception as e:
                # Unexpected error - don't retry
                logger.error(f"Unexpected error sending command: {e}")
                self.disconnect()
                return {"status": "error", "error": str(e)}
        
        return {"status": "error", "error": f"Command failed after {self.MAX_RETRIES + 1} attempts: {last_error}"}

    def _send_command_once(self, command: str, params: Dict[str, Any], attempt: int) -> Dict[str, Any]:
        """
        Send command once (internal method).
        
        Args:
            command: Command type
            params: Command parameters
            attempt: Current attempt number
            
        Returns:
            Response dictionary
            
        Raises:
            Various exceptions on failure
        """
        # Hold lock for entire send-receive cycle to prevent race conditions
        # where another thread could close/reconnect the socket mid-operation.
        # RLock allows nested acquisition from connect()/disconnect() calls.
        with self._lock:
            # Connect (or reconnect)
            if not self.connect():
                raise ConnectionError(f"Failed to connect to Unreal Engine: {self._last_error}")
            
            try:
                # Build and send command
                command_obj = {
                    "type": command,
                    "params": params or {}
                }
                command_json = json.dumps(command_obj)
                
                logger.info(f"Sending command (attempt {attempt + 1}): {command}")
                logger.debug(f"Command payload: {command_json[:500]}...")
                
                # Send with timeout
                self.socket.settimeout(10)  # 10 second send timeout
                self.socket.sendall(command_json.encode('utf-8'))
                
                # Receive response
                response_data = self._receive_response(command)
                
                # Parse response
                try:
                    response = json.loads(response_data.decode('utf-8'))
                except json.JSONDecodeError as e:
                    logger.error(f"JSON decode error: {e}")
                    logger.debug(f"Raw response: {response_data[:500]}")
                    raise ValueError(f"Invalid JSON response: {e}")
                
                logger.info(f"Command {command} completed successfully")
                
                # Normalize error responses
                if response.get("status") == "error":
                    error_msg = response.get("error") or response.get("message", "Unknown error")
                    logger.warning(f"Unreal returned error: {error_msg}")
                elif response.get("success") is False:
                    error_msg = response.get("error") or response.get("message", "Unknown error")
                    response = {"status": "error", "error": error_msg}
                    logger.warning(f"Unreal returned failure: {error_msg}")
                
                return response
                
            finally:
                # Always clean up connection after command
                self._close_socket_unsafe()

# Global connection instance (singleton pattern)
_unreal_connection: Optional[UnrealConnection] = None
_connection_lock = threading.Lock()

def get_unreal_connection() -> UnrealConnection:
    """
    Get the global Unreal connection instance.
    
    Uses lazy initialization - connection is created on first access.
    The connection handles its own retry logic, so we don't need to
    pre-connect here.
    
    Returns:
        UnrealConnection instance (always returns an instance, never None)
    """
    global _unreal_connection
    
    with _connection_lock:
        if _unreal_connection is None:
            logger.info("Creating new UnrealConnection instance")
            _unreal_connection = UnrealConnection()
        return _unreal_connection


def reset_unreal_connection():
    """Reset the global connection (useful for error recovery)."""
    global _unreal_connection
    
    with _connection_lock:
        if _unreal_connection:
            _unreal_connection.disconnect()
            _unreal_connection = None
        logger.info("Unreal connection reset")

@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    """Handle server startup and shutdown."""
    logger.info("UnrealMCP Advanced server starting up")
    logger.info("Connection will be established lazily on first tool call")

    try:
        yield {}
    finally:
        reset_unreal_connection()
        logger.info("Unreal MCP Advanced server shut down")

# Initialize server
mcp = FastMCP(
    "UnrealMCP_Advanced",
    lifespan=server_lifespan
)

# Essential Actor Management Tools
@mcp.tool()
def get_actors_in_level(random_string: str = "") -> Dict[str, Any]:
    """Get a list of all actors in the current level."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        response = unreal.send_command("get_actors_in_level", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_actors_in_level error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def find_actors_by_name(pattern: str) -> Dict[str, Any]:
    """Find actors by name pattern."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        response = unreal.send_command("find_actors_by_name", {"pattern": pattern})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"find_actors_by_name error: {e}")
        return {"success": False, "message": str(e)}



@mcp.tool()
def delete_actor(name: str) -> Dict[str, Any]:
    """Delete an actor by name."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        # Use the safe delete function to update tracking
        response = safe_delete_actor(unreal, name)
        return response
    except Exception as e:
        logger.error(f"delete_actor error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_actor_transform(
    name: str,
    location: List[float] = None,
    rotation: List[float] = None,
    scale: List[float] = None
) -> Dict[str, Any]:
    """Set the transform of an actor."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {"name": name}
        if location is not None:
            params["location"] = location
        if rotation is not None:
            params["rotation"] = rotation
        if scale is not None:
            params["scale"] = scale
            
        response = unreal.send_command("set_actor_transform", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_actor_transform error: {e}")
        return {"success": False, "message": str(e)}

# Essential Blueprint Tools for Physics Actors
@mcp.tool()
def create_blueprint(name: str, parent_class: str) -> Dict[str, Any]:
    """Create a new Blueprint class."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "name": name,
            "parent_class": parent_class
        }
        response = unreal.send_command("create_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_blueprint error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def add_component_to_blueprint(
    blueprint_name: str,
    component_type: str,
    component_name: str,
    location: List[float] = [],
    rotation: List[float] = [],
    scale: List[float] = [],
    component_properties: Dict[str, Any] = {}
) -> Dict[str, Any]:
    """Add a component to a Blueprint."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_name": blueprint_name,
            "component_type": component_type,
            "component_name": component_name,
            "location": location,
            "rotation": rotation,
            "scale": scale,
            "component_properties": component_properties
        }
        response = unreal.send_command("add_component_to_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_component_to_blueprint error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_static_mesh_properties(
    blueprint_name: str,
    component_name: str,
    static_mesh: str = "/Engine/BasicShapes/Cube.Cube"
) -> Dict[str, Any]:
    """Set static mesh properties on a StaticMeshComponent."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "static_mesh": static_mesh
        }
        response = unreal.send_command("set_static_mesh_properties", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_static_mesh_properties error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_physics_properties(
    blueprint_name: str,
    component_name: str,
    simulate_physics: bool = True,
    gravity_enabled: bool = True,
    mass: float = 1,
    linear_damping: float = 0.01,
    angular_damping: float = 0
) -> Dict[str, Any]:
    """Set physics properties on a component."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "simulate_physics": simulate_physics,
            "gravity_enabled": gravity_enabled,
            "mass": mass,
            "linear_damping": linear_damping,
            "angular_damping": angular_damping
        }
        response = unreal.send_command("set_physics_properties", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_physics_properties error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def compile_blueprint(blueprint_name: str) -> Dict[str, Any]:
    """Compile a Blueprint."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {"blueprint_name": blueprint_name}
        response = unreal.send_command("compile_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"compile_blueprint error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def read_blueprint_content(
    blueprint_path: str,
    include_event_graph: bool = True,
    include_functions: bool = True,
    include_variables: bool = True,
    include_components: bool = True,
    include_interfaces: bool = True
) -> Dict[str, Any]:
    """
    Read and analyze the complete content of a Blueprint including event graph, 
    functions, variables, components, and implemented interfaces.
    
    Args:
        blueprint_path: Full path to the Blueprint asset (e.g., "/Game/MyBlueprint.MyBlueprint")
        include_event_graph: Include event graph nodes and connections
        include_functions: Include custom functions and their graphs
        include_variables: Include all Blueprint variables with types and defaults
        include_components: Include component hierarchy and properties
        include_interfaces: Include implemented Blueprint interfaces
    
    Returns:
        Dictionary containing complete Blueprint structure and content
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_path": blueprint_path,
            "include_event_graph": include_event_graph,
            "include_functions": include_functions,
            "include_variables": include_variables,
            "include_components": include_components,
            "include_interfaces": include_interfaces
        }
        
        logger.info(f"Reading Blueprint content for: {blueprint_path}")
        response = unreal.send_command("read_blueprint_content", params)
        
        if response and response.get("success", False):
            logger.info(f"Successfully read Blueprint content. Found:")
            if response.get("variables"):
                logger.info(f"  - {len(response['variables'])} variables")
            if response.get("functions"):
                logger.info(f"  - {len(response['functions'])} functions")
            if response.get("event_graph", {}).get("nodes"):
                logger.info(f"  - {len(response['event_graph']['nodes'])} event graph nodes")
            if response.get("components"):
                logger.info(f"  - {len(response['components'])} components")
        
        return response or {"success": False, "message": "No response from Unreal"}
        
    except Exception as e:
        logger.error(f"read_blueprint_content error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def analyze_blueprint_graph(
    blueprint_path: str,
    graph_name: str = "EventGraph",
    include_node_details: bool = True,
    include_pin_connections: bool = True,
    trace_execution_flow: bool = True
) -> Dict[str, Any]:
    """
    Analyze a specific graph within a Blueprint (EventGraph, functions, etc.)
    and provide detailed information about nodes, connections, and execution flow.
    
    Args:
        blueprint_path: Full path to the Blueprint asset
        graph_name: Name of the graph to analyze ("EventGraph", function name, etc.)
        include_node_details: Include detailed node properties and settings
        include_pin_connections: Include all pin-to-pin connections
        trace_execution_flow: Trace the execution flow through the graph
    
    Returns:
        Dictionary with graph analysis including nodes, connections, and flow
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_path": blueprint_path,
            "graph_name": graph_name,
            "include_node_details": include_node_details,
            "include_pin_connections": include_pin_connections,
            "trace_execution_flow": trace_execution_flow
        }
        
        logger.info(f"Analyzing Blueprint graph: {blueprint_path} -> {graph_name}")
        response = unreal.send_command("analyze_blueprint_graph", params)
        
        if response and response.get("success", False):
            graph_data = response.get("graph_data", {})
            logger.info(f"Graph analysis complete:")
            logger.info(f"  - Graph: {graph_data.get('graph_name', 'Unknown')}")
            logger.info(f"  - Nodes: {len(graph_data.get('nodes', []))}")
            logger.info(f"  - Connections: {len(graph_data.get('connections', []))}")
            if graph_data.get('execution_paths'):
                logger.info(f"  - Execution paths: {len(graph_data['execution_paths'])}")
        
        return response or {"success": False, "message": "No response from Unreal"}
        
    except Exception as e:
        logger.error(f"analyze_blueprint_graph error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_blueprint_variable_details(
    blueprint_path: str,
    variable_name: str = None
) -> Dict[str, Any]:
    """
    Get detailed information about Blueprint variables including type, 
    default values, metadata, and usage within the Blueprint.
    
    Args:
        blueprint_path: Full path to the Blueprint asset
        variable_name: Specific variable name (if None, returns all variables)
    
    Returns:
        Dictionary with variable details including type, defaults, and usage
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_path": blueprint_path,
            "variable_name": variable_name
        }
        
        logger.info(f"Getting Blueprint variable details: {blueprint_path}")
        if variable_name:
            logger.info(f"  - Specific variable: {variable_name}")
        
        response = unreal.send_command("get_blueprint_variable_details", params)
        return response or {"success": False, "message": "No response from Unreal"}
        
    except Exception as e:
        logger.error(f"get_blueprint_variable_details error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_blueprint_function_details(
    blueprint_path: str,
    function_name: str = None,
    include_graph: bool = True
) -> Dict[str, Any]:
    """
    Get detailed information about Blueprint functions including parameters,
    return values, local variables, and function graph content.
    
    Args:
        blueprint_path: Full path to the Blueprint asset
        function_name: Specific function name (if None, returns all functions)
        include_graph: Include the function's graph nodes and connections
    
    Returns:
        Dictionary with function details including signature and graph content
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_path": blueprint_path,
            "function_name": function_name,
            "include_graph": include_graph
        }
        
        logger.info(f"Getting Blueprint function details: {blueprint_path}")
        if function_name:
            logger.info(f"  - Specific function: {function_name}")
        
        response = unreal.send_command("get_blueprint_function_details", params)
        return response or {"success": False, "message": "No response from Unreal"}
        
    except Exception as e:
        logger.error(f"get_blueprint_function_details error: {e}")
        return {"success": False, "message": str(e)}



# Advanced Composition Tools
@mcp.tool()
def create_pyramid(
    base_size: int = 3,
    block_size: float = 100.0,
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "PyramidBlock",
    mesh: str = "/Engine/BasicShapes/Cube.Cube"
) -> Dict[str, Any]:
    """Spawn a pyramid made of cube actors."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        spawned = []
        scale = block_size / 100.0
        for level in range(base_size):
            count = base_size - level
            for x in range(count):
                for y in range(count):
                    actor_name = f"{name_prefix}_{level}_{x}_{y}"
                    loc = [
                        location[0] + (x - (count - 1)/2) * block_size,
                        location[1] + (y - (count - 1)/2) * block_size,
                        location[2] + level * block_size
                    ]
                    params = {
                        "name": actor_name,
                        "type": "StaticMeshActor",
                        "location": loc,
                        "scale": [scale, scale, scale],
                        "static_mesh": mesh
                    }
                    resp = safe_spawn_actor(unreal, params)
                    if resp and resp.get("status") == "success":
                        spawned.append(resp)
        return {"success": True, "actors": spawned}
    except Exception as e:
        logger.error(f"create_pyramid error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_wall(
    length: int = 5,
    height: int = 2,
    block_size: float = 100.0,
    location: List[float] = [0.0, 0.0, 0.0],
    orientation: str = "x",
    name_prefix: str = "WallBlock",
    mesh: str = "/Engine/BasicShapes/Cube.Cube"
) -> Dict[str, Any]:
    """Create a simple wall from cubes."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        spawned = []
        scale = block_size / 100.0
        for h in range(height):
            for i in range(length):
                actor_name = f"{name_prefix}_{h}_{i}"
                if orientation == "x":
                    loc = [location[0] + i * block_size, location[1], location[2] + h * block_size]
                else:
                    loc = [location[0], location[1] + i * block_size, location[2] + h * block_size]
                params = {
                    "name": actor_name,
                    "type": "StaticMeshActor",
                    "location": loc,
                    "scale": [scale, scale, scale],
                    "static_mesh": mesh
                }
                resp = safe_spawn_actor(unreal, params)
                if resp and resp.get("status") == "success":
                    spawned.append(resp)
        return {"success": True, "actors": spawned}
    except Exception as e:
        logger.error(f"create_wall error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_tower(
    height: int = 10,
    base_size: int = 4,
    block_size: float = 100.0,
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "TowerBlock",
    mesh: str = "/Engine/BasicShapes/Cube.Cube",
    tower_style: str = "cylindrical"  # "cylindrical", "square", "tapered"
) -> Dict[str, Any]:
    """Create a realistic tower with various architectural styles."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        spawned = []
        scale = block_size / 100.0

        for level in range(height):
            level_height = location[2] + level * block_size
            
            if tower_style == "cylindrical":
                # Create circular tower
                radius = (base_size / 2) * block_size  # Convert to world units (centimeters)
                circumference = 2 * math.pi * radius
                num_blocks = max(8, int(circumference / block_size))
                
                for i in range(num_blocks):
                    angle = (2 * math.pi * i) / num_blocks
                    x = location[0] + radius * math.cos(angle)
                    y = location[1] + radius * math.sin(angle)
                    
                    actor_name = f"{name_prefix}_{level}_{i}"
                    params = {
                        "name": actor_name,
                        "type": "StaticMeshActor",
                        "location": [x, y, level_height],
                        "scale": [scale, scale, scale],
                        "static_mesh": mesh
                    }
                    resp = safe_spawn_actor(unreal, params)
                    if resp and resp.get("status") == "success":
                        spawned.append(resp)
                        
            elif tower_style == "tapered":
                # Create tapering square tower
                current_size = max(1, base_size - (level // 2))
                half_size = current_size / 2
                
                # Create walls for current level
                for side in range(4):
                    for i in range(current_size):
                        if side == 0:  # Front wall
                            x = location[0] + (i - half_size + 0.5) * block_size
                            y = location[1] - half_size * block_size
                            actor_name = f"{name_prefix}_{level}_front_{i}"
                        elif side == 1:  # Right wall
                            x = location[0] + half_size * block_size
                            y = location[1] + (i - half_size + 0.5) * block_size
                            actor_name = f"{name_prefix}_{level}_right_{i}"
                        elif side == 2:  # Back wall
                            x = location[0] + (half_size - i - 0.5) * block_size
                            y = location[1] + half_size * block_size
                            actor_name = f"{name_prefix}_{level}_back_{i}"
                        else:  # Left wall
                            x = location[0] - half_size * block_size
                            y = location[1] + (half_size - i - 0.5) * block_size
                            actor_name = f"{name_prefix}_{level}_left_{i}"
                            
                        params = {
                            "name": actor_name,
                            "type": "StaticMeshActor",
                            "location": [x, y, level_height],
                            "scale": [scale, scale, scale],
                            "static_mesh": mesh
                        }
                        resp = unreal.send_command("spawn_actor", params)
                        if resp:
                            spawned.append(resp)
                            
            else:  # square tower
                # Create square tower walls
                half_size = base_size / 2
                
                # Four walls
                for side in range(4):
                    for i in range(base_size):
                        if side == 0:  # Front wall
                            x = location[0] + (i - half_size + 0.5) * block_size
                            y = location[1] - half_size * block_size
                            actor_name = f"{name_prefix}_{level}_front_{i}"
                        elif side == 1:  # Right wall
                            x = location[0] + half_size * block_size
                            y = location[1] + (i - half_size + 0.5) * block_size
                            actor_name = f"{name_prefix}_{level}_right_{i}"
                        elif side == 2:  # Back wall
                            x = location[0] + (half_size - i - 0.5) * block_size
                            y = location[1] + half_size * block_size
                            actor_name = f"{name_prefix}_{level}_back_{i}"
                        else:  # Left wall
                            x = location[0] - half_size * block_size
                            y = location[1] + (half_size - i - 0.5) * block_size
                            actor_name = f"{name_prefix}_{level}_left_{i}"
                            
                        params = {
                            "name": actor_name,
                            "type": "StaticMeshActor",
                            "location": [x, y, level_height],
                            "scale": [scale, scale, scale],
                            "static_mesh": mesh
                        }
                        resp = unreal.send_command("spawn_actor", params)
                        if resp:
                            spawned.append(resp)
                            
            # Add decorative elements every few levels
            if level % 3 == 2 and level < height - 1:
                # Add corner details
                for corner in range(4):
                    angle = corner * math.pi / 2
                    detail_x = location[0] + (base_size/2 + 0.5) * block_size * math.cos(angle)
                    detail_y = location[1] + (base_size/2 + 0.5) * block_size * math.sin(angle)
                    
                    actor_name = f"{name_prefix}_{level}_detail_{corner}"
                    params = {
                        "name": actor_name,
                        "type": "StaticMeshActor",
                        "location": [detail_x, detail_y, level_height],
                        "scale": [scale * 0.7, scale * 0.7, scale * 0.7],
                        "static_mesh": "/Engine/BasicShapes/Cylinder.Cylinder"
                    }
                    resp = safe_spawn_actor(unreal, params)
                    if resp and resp.get("status") == "success":
                        spawned.append(resp)
                        
        return {"success": True, "actors": spawned, "tower_style": tower_style}
    except Exception as e:
        logger.error(f"create_tower error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_staircase(
    steps: int = 5,
    step_size: List[float] = [100.0, 100.0, 50.0],
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "Stair",
    mesh: str = "/Engine/BasicShapes/Cube.Cube"
) -> Dict[str, Any]:
    """Create a staircase from cubes."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        spawned = []
        sx, sy, sz = step_size
        for i in range(steps):
            actor_name = f"{name_prefix}_{i}"
            loc = [location[0] + i * sx, location[1], location[2] + i * sz]
            scale = [sx/100.0, sy/100.0, sz/100.0]
            params = {
                "name": actor_name,
                "type": "StaticMeshActor",
                "location": loc,
                "scale": scale,
                "static_mesh": mesh
            }
            resp = safe_spawn_actor(unreal, params)
            if resp and resp.get("status") == "success":
                spawned.append(resp)
        return {"success": True, "actors": spawned}
    except Exception as e:
        logger.error(f"create_staircase error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def construct_house(
    width: int = 1200,
    depth: int = 1000,
    height: int = 600,
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "House",
    mesh: str = "/Engine/BasicShapes/Cube.Cube",
    house_style: str = "modern"  # "modern", "cottage"
) -> Dict[str, Any]:
    """Construct a realistic house with architectural details and multiple rooms."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}

        # Use the helper function to build the house
        return build_house(unreal, width, depth, height, location, name_prefix, mesh, house_style)

    except Exception as e:
        logger.error(f"construct_house error: {e}")
        return {"success": False, "message": str(e)}



@mcp.tool()
def construct_mansion(
    mansion_scale: str = "large",  # "small", "large", "epic", "legendary"
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "Mansion"
) -> Dict[str, Any]:
    """
    Construct a magnificent mansion with multiple wings, grand rooms, gardens,
    fountains, and luxury features perfect for dramatic TikTok reveals.
    """
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}

        logger.info(f"Creating {mansion_scale} mansion")
        all_actors = []

        # Get size parameters and calculate scaled dimensions
        params = get_mansion_size_params(mansion_scale)
        layout = calculate_mansion_layout(params)

        # Build mansion main structure
        build_mansion_main_structure(unreal, name_prefix, location, layout, all_actors)

        # Build mansion exterior
        build_mansion_exterior(unreal, name_prefix, location, layout, all_actors)

        # Add luxurious interior
        add_mansion_interior(unreal, name_prefix, location, layout, all_actors)

        logger.info(f"Mansion construction complete! Created {len(all_actors)} elements")

        return {
            "success": True,
            "message": f"Magnificent {mansion_scale} mansion created with {len(all_actors)} elements!",
            "actors": all_actors,
            "stats": {
                "scale": mansion_scale,
                "wings": layout["wings"],
                "floors": layout["floors"],
                "main_rooms": layout["main_rooms"],
                "bedrooms": layout["bedrooms"],
                "garden_size": layout["garden_size"],
                "fountain_count": layout["fountain_count"],
                "car_count": layout["car_count"],
                "total_actors": len(all_actors)
            }
        }

    except Exception as e:
        logger.error(f"construct_mansion error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_arch(
    radius: float = 300.0,
    segments: int = 6,
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "ArchBlock",
    mesh: str = "/Engine/BasicShapes/Cube.Cube"
) -> Dict[str, Any]:
    """Create a simple arch using cubes in a semicircle."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        spawned = []
        angle_step = math.pi / segments
        scale = radius / 300.0 / 2
        for i in range(segments + 1):
            theta = angle_step * i
            x = radius * math.cos(theta)
            z = radius * math.sin(theta)
            actor_name = f"{name_prefix}_{i}"
            params = {
                "name": actor_name,
                "type": "StaticMeshActor",
                "location": [location[0] + x, location[1], location[2] + z],
                "scale": [scale, scale, scale],
                "static_mesh": mesh
            }
            resp = safe_spawn_actor(unreal, params)
            if resp and resp.get("status") == "success":
                spawned.append(resp)
        return {"success": True, "actors": spawned}
    except Exception as e:
        logger.error(f"create_arch error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def spawn_physics_blueprint_actor (
    name: str,
    mesh_path: str = "/Engine/BasicShapes/Cube.Cube",
    location: List[float] = [0.0, 0.0, 0.0],
    mass: float = 1.0,
    simulate_physics: bool = True,
    gravity_enabled: bool = True,
    color: List[float] = None,  # Optional color parameter [R, G, B] or [R, G, B, A]
    scale: List[float] = [1.0, 1.0, 1.0]  # Default scale
) -> Dict[str, Any]:
    """
    Quickly spawn a single actor with physics, color, and a specific mesh.

    This is the primary function for creating simple objects with physics properties.
    It handles creating a temporary Blueprint, setting up the mesh, color, and physics,
    and then spawns the actor in the world. It's ideal for quickly adding
    dynamic objects to the scene without needing to manually create Blueprints.
    
    Args:
        color: Optional color as [R, G, B] or [R, G, B, A] where values are 0.0-1.0.
               If [R, G, B] is provided, alpha will be set to 1.0 automatically.
    """
    try:
        bp_name = f"{name}_BP"
        create_blueprint(bp_name, "Actor")
        add_component_to_blueprint(bp_name, "StaticMeshComponent", "Mesh", scale=scale)
        set_static_mesh_properties(bp_name, "Mesh", mesh_path)
        set_physics_properties(bp_name, "Mesh", simulate_physics, gravity_enabled, mass)

        # Set color if provided
        if color is not None:
            # Convert 3-value color [R,G,B] to 4-value [R,G,B,A] if needed
            if len(color) == 3:
                color = color + [1.0]  # Add alpha=1.0
            elif len(color) != 4:
                logger.warning(f"Invalid color format: {color}. Expected [R,G,B] or [R,G,B,A]. Skipping color.")
                color = None

            if color is not None:
                color_result = set_mesh_material_color(bp_name, "Mesh", color)
                if not color_result.get("success", False):
                    logger.warning(f"Failed to set color {color} for {bp_name}: {color_result.get('message', 'Unknown error')}")

        compile_blueprint(bp_name)
        result = spawn_blueprint_actor(bp_name, name, location)
        
        # Spawn the blueprint actor using helper function
        unreal = get_unreal_connection()
        result = spawn_blueprint_actor(unreal, bp_name, name, location)

        # Ensure proper scale is set on the spawned actor
        if result.get("success", False):
            spawned_name = result.get("result", {}).get("name", name)
            set_actor_transform(spawned_name, scale=scale)

        return result
    except Exception as e:
        logger.error(f"spawn_physics_blueprint_actor  error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_maze(
    rows: int = 8,
    cols: int = 8,
    cell_size: float = 300.0,
    wall_height: int = 3,
    location: List[float] = [0.0, 0.0, 0.0]
) -> Dict[str, Any]:
    """Create a proper solvable maze with entrance, exit, and guaranteed path using recursive backtracking algorithm."""
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
        import random
        spawned = []
        
        # Initialize maze grid - True means wall, False means open
        maze = [[True for _ in range(cols * 2 + 1)] for _ in range(rows * 2 + 1)]
        
        # Recursive backtracking maze generation
        def carve_path(row, col):
            # Mark current cell as path
            maze[row * 2 + 1][col * 2 + 1] = False
            
            # Random directions
            directions = [(0, 1), (1, 0), (0, -1), (-1, 0)]
            random.shuffle(directions)
            
            for dr, dc in directions:
                new_row, new_col = row + dr, col + dc
                
                # Check bounds
                if (0 <= new_row < rows and 0 <= new_col < cols and 
                    maze[new_row * 2 + 1][new_col * 2 + 1]):
                    
                    # Carve wall between current and new cell
                    maze[row * 2 + 1 + dr][col * 2 + 1 + dc] = False
                    carve_path(new_row, new_col)
        
        # Start carving from top-left corner
        carve_path(0, 0)
        
        # Create entrance and exit
        maze[1][0] = False  # Entrance on left side
        maze[rows * 2 - 1][cols * 2] = False  # Exit on right side
        
        # Build the actual maze in Unreal
        maze_height = rows * 2 + 1
        maze_width = cols * 2 + 1
        
        for r in range(maze_height):
            for c in range(maze_width):
                if maze[r][c]:  # If this is a wall
                    # Stack blocks to create wall height
                    for h in range(wall_height):
                        x_pos = location[0] + (c - maze_width/2) * cell_size
                        y_pos = location[1] + (r - maze_height/2) * cell_size
                        z_pos = location[2] + h * cell_size
                        
                        actor_name = f"Maze_Wall_{r}_{c}_{h}"
                        params = {
                            "name": actor_name,
                            "type": "StaticMeshActor",
                            "location": [x_pos, y_pos, z_pos],
                            "scale": [cell_size/100.0, cell_size/100.0, cell_size/100.0],
                            "static_mesh": "/Engine/BasicShapes/Cube.Cube"
                        }
                        resp = safe_spawn_actor(unreal, params)
                        if resp and resp.get("status") == "success":
                            spawned.append(resp)
        
        # Add entrance and exit markers
        entrance_marker = safe_spawn_actor(unreal, {
            "name": "Maze_Entrance",
            "type": "StaticMeshActor",
            "location": [location[0] - maze_width/2 * cell_size - cell_size, 
                       location[1] + (-maze_height/2 + 1) * cell_size, 
                       location[2] + cell_size],
            "scale": [0.5, 0.5, 0.5],
            "static_mesh": "/Engine/BasicShapes/Cylinder.Cylinder"
        })
        if entrance_marker and entrance_marker.get("status") == "success":
            spawned.append(entrance_marker)
            
        exit_marker = safe_spawn_actor(unreal, {
            "name": "Maze_Exit",
            "type": "StaticMeshActor", 
            "location": [location[0] + maze_width/2 * cell_size + cell_size,
                       location[1] + (-maze_height/2 + rows * 2 - 1) * cell_size,
                       location[2] + cell_size],
            "scale": [0.5, 0.5, 0.5],
            "static_mesh": "/Engine/BasicShapes/Sphere.Sphere"
        })
        if exit_marker and exit_marker.get("status") == "success":
            spawned.append(exit_marker)
        
        return {
            "success": True, 
            "actors": spawned, 
            "maze_size": f"{rows}x{cols}",
            "wall_count": len([block for block in spawned if "Wall" in block.get("name", "")]),
            "entrance": "Left side (cylinder marker)",
            "exit": "Right side (sphere marker)"
        }
    except Exception as e:
        logger.error(f"create_maze error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_available_materials(
    search_path: str = "/Game/",
    include_engine_materials: bool = True
) -> Dict[str, Any]:
    """Get a list of available materials in the project that can be applied to objects."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "search_path": search_path,
            "include_engine_materials": include_engine_materials
        }
        response = unreal.send_command("get_available_materials", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_available_materials error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def apply_material_to_actor(
    actor_name: str,
    material_path: str,
    material_slot: int = 0
) -> Dict[str, Any]:
    """Apply a specific material to an actor in the level."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "actor_name": actor_name,
            "material_path": material_path,
            "material_slot": material_slot
        }
        response = unreal.send_command("apply_material_to_actor", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"apply_material_to_actor error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def apply_material_to_blueprint(
    blueprint_name: str,
    component_name: str,
    material_path: str,
    material_slot: int = 0
) -> Dict[str, Any]:
    """Apply a specific material to a component in a Blueprint."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "material_path": material_path,
            "material_slot": material_slot
        }
        response = unreal.send_command("apply_material_to_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"apply_material_to_blueprint error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_actor_material_info(
    actor_name: str
) -> Dict[str, Any]:
    """Get information about the materials currently applied to an actor."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        params = {"actor_name": actor_name}
        response = unreal.send_command("get_actor_material_info", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_actor_material_info error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_material_instance_parameters(
    material_path: str,
    parameter_types: list = None,
    overridden_only: bool = False,
    name_filter: str = None,
    include_metadata: bool = True
) -> Dict[str, Any]:
    """Get parameter values from a Material Instance (scalars, vectors, textures, static switches, etc.).

    Returns parameter names, values, groups, and whether each is overridden vs inherited from parent.
    Works with Material Instance Constants. If a base Material path is given, returns a note instead.

    Filters (all optional):
    - parameter_types: list of types to include: "scalar", "vector", "texture", "static_switch",
      "static_component_mask", "runtime_virtual_texture". Default: all types.
    - overridden_only: if true, only return parameters that are overridden in this instance (not inherited defaults).
    - name_filter: case-insensitive substring match on parameter name (e.g. "Roughness", "Snow").
    - include_metadata: if true (default), include group_name, description, and min/max for scalars.

    For large material instances (e.g. landscape), use parameter_types and/or name_filter to reduce output size.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        if parameter_types is not None:
            params["parameter_types"] = parameter_types
        if overridden_only:
            params["overridden_only"] = True
        if name_filter is not None:
            params["name_filter"] = name_filter
        if not include_metadata:
            params["include_metadata"] = False
        response = unreal.send_command("get_material_instance_parameters", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_instance_parameters error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_material_instance_parameters(
    material_path: str,
    scalar_parameters: list = None,
    vector_parameters: list = None,
    texture_parameters: list = None,
    static_switch_parameters: list = None
) -> Dict[str, Any]:
    """Set parameter values on a Material Instance Constant (scalars, vectors, textures, static switches).

    Each parameter type takes a list of objects:
    - scalar_parameters: [{"name": "Roughness", "value": 0.5}, ...]
    - vector_parameters: [{"name": "BaseColor", "value": [1.0, 0.5, 0.2, 1.0]}, ...]
    - texture_parameters: [{"name": "Diffuse", "value": "/Game/Textures/T_Diff"}, ...] (use "" or "None" to clear)
    - static_switch_parameters: [{"name": "UseNormalMap", "value": true}, ...]

    Per-parameter errors are collected in the 'errors' array without stopping other parameters from being set.
    Changes are marked dirty in the editor (requires manual save).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        if scalar_parameters is not None:
            params["scalar_parameters"] = scalar_parameters
        if vector_parameters is not None:
            params["vector_parameters"] = vector_parameters
        if texture_parameters is not None:
            params["texture_parameters"] = texture_parameters
        if static_switch_parameters is not None:
            params["static_switch_parameters"] = static_switch_parameters

        response = unreal.send_command("set_material_instance_parameters", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_material_instance_parameters error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_mesh_material_color(
    blueprint_name: str,
    component_name: str,
    color: List[float],
    material_path: str = "/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name: str = "BaseColor",
    material_slot: int = 0
) -> Dict[str, Any]:
    """Set material color on a mesh component using the proven color system."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    
    try:
        # Validate color format
        if not isinstance(color, list) or len(color) != 4:
            return {"success": False, "message": "Invalid color format. Must be a list of 4 float values [R, G, B, A]."}
        
        # Ensure all color values are floats between 0 and 1
        color = [float(min(1.0, max(0.0, val))) for val in color]
        
        # Set BaseColor parameter first
        params_base = {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "color": color,
            "material_path": material_path,
            "parameter_name": "BaseColor",
            "material_slot": material_slot
        }
        response_base = unreal.send_command("set_mesh_material_color", params_base)
        
        # Set Color parameter second (for maximum compatibility)
        params_color = {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "color": color,
            "material_path": material_path,
            "parameter_name": "Color",
            "material_slot": material_slot
        }
        response_color = unreal.send_command("set_mesh_material_color", params_color)
        
        # Return success if either parameter setting worked
        if (response_base and response_base.get("status") == "success") or (response_color and response_color.get("status") == "success"):
            return {
                "success": True, 
                "message": f"Color applied successfully to slot {material_slot}: {color}",
                "base_color_result": response_base,
                "color_result": response_color,
                "material_slot": material_slot
            }
        else:
            return {
                "success": False, 
                "message": f"Failed to set color parameters on slot {material_slot}. BaseColor: {response_base}, Color: {response_color}"
            }
            
    except Exception as e:
        logger.error(f"set_mesh_material_color error: {e}")
        return {"success": False, "message": str(e)}

# Advanced Town Generation System
@mcp.tool()
def create_town(
    town_size: str = "medium",  # "small", "medium", "large", "metropolis"
    building_density: float = 0.7,  # 0.0 to 1.0
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "Town",
    include_infrastructure: bool = True,
    architectural_style: str = "mixed"  # "modern", "cottage", "mansion", "mixed", "downtown", "futuristic"
) -> Dict[str, Any]:
    """Create a full dynamic town with buildings, streets, infrastructure, and vehicles."""
    try:
        import random
        random.seed()  # Use different seed each time for variety
        
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        
        logger.info(f"Creating {town_size} town with {building_density} density at {location}")
        
        # Define town parameters based on size
        town_params = {
            "small": {"blocks": 3, "block_size": 1500, "max_building_height": 5, "population": 20, "skyscraper_chance": 0.1},
            "medium": {"blocks": 5, "block_size": 2000, "max_building_height": 10, "population": 50, "skyscraper_chance": 0.3},
            "large": {"blocks": 7, "block_size": 2500, "max_building_height": 20, "population": 100, "skyscraper_chance": 0.5},
            "metropolis": {"blocks": 10, "block_size": 3000, "max_building_height": 40, "population": 200, "skyscraper_chance": 0.7}
        }
        
        params = town_params.get(town_size, town_params["medium"])
        blocks = params["blocks"]
        block_size = params["block_size"]
        max_height = params["max_building_height"]
        target_population = int(params["population"] * building_density)
        skyscraper_chance = params["skyscraper_chance"]
        
        all_spawned = []
        street_width = block_size * 0.3
        building_area = block_size * 0.7
        
        # Create street grid first
        logger.info("Creating street grid...")
        street_results = _create_street_grid(blocks, block_size, street_width, location, name_prefix)
        all_spawned.extend(street_results.get("actors", []))
        
        # Create buildings in each block
        logger.info("Placing buildings...")
        building_count = 0
        for block_x in range(blocks):
            for block_y in range(blocks):
                if building_count >= target_population:
                    break
                    
                # Skip some blocks randomly for variety
                if random.random() > building_density:
                    continue
                
                block_center_x = location[0] + (block_x - blocks/2) * block_size
                block_center_y = location[1] + (block_y - blocks/2) * block_size
                
                # Randomly choose building type based on style and location
                if architectural_style == "downtown" or architectural_style == "futuristic":
                    building_types = ["skyscraper", "office_tower", "apartment_complex", "shopping_mall", "parking_garage", "hotel"]
                elif architectural_style == "mixed":
                    # Central blocks get taller buildings
                    is_central = abs(block_x - blocks//2) <= 1 and abs(block_y - blocks//2) <= 1
                    if is_central and random.random() < skyscraper_chance:
                        building_types = ["skyscraper", "office_tower", "apartment_complex", "hotel", "shopping_mall"]
                    else:
                        building_types = ["house", "tower", "mansion", "commercial", "apartment_building", "restaurant", "store"]
                else:
                    building_types = [architectural_style] * 3 + ["commercial", "restaurant", "store"]
                
                building_type = random.choice(building_types)
                
                # Create building with variety
                building_result = _create_town_building(
                    building_type, 
                    [block_center_x, block_center_y, location[2]],
                    building_area,
                    max_height,
                    f"{name_prefix}_Building_{block_x}_{block_y}",
                    building_count
                )
                
                if building_result.get("status") == "success":
                    all_spawned.extend(building_result.get("actors", []))
                    building_count += 1
        
        # Add infrastructure if requested
        infrastructure_count = 0
        if include_infrastructure:
            logger.info("Adding infrastructure...")
            
            # Street lights
            light_results = _create_street_lights(blocks, block_size, location, name_prefix)
            all_spawned.extend(light_results.get("actors", []))
            infrastructure_count += len(light_results.get("actors", []))
            
            # Vehicles
            vehicle_results = _create_town_vehicles(blocks, block_size, street_width, location, name_prefix, target_population // 3)
            all_spawned.extend(vehicle_results.get("actors", []))
            infrastructure_count += len(vehicle_results.get("actors", []))
            
            # Parks and decorations
            decoration_results = _create_town_decorations(blocks, block_size, location, name_prefix)
            all_spawned.extend(decoration_results.get("actors", []))
            infrastructure_count += len(decoration_results.get("actors", []))
            
            
            # Add advanced infrastructure
            logger.info("Adding advanced infrastructure...")
            
            # Traffic lights at intersections
            traffic_results = _create_traffic_lights(blocks, block_size, location, name_prefix)
            all_spawned.extend(traffic_results.get("actors", []))
            infrastructure_count += len(traffic_results.get("actors", []))
            
            # Street signs and billboards
            signage_results = _create_street_signage(blocks, block_size, location, name_prefix, town_size)
            all_spawned.extend(signage_results.get("actors", []))
            infrastructure_count += len(signage_results.get("actors", []))
            
            # Sidewalks and crosswalks
            sidewalk_results = _create_sidewalks_crosswalks(blocks, block_size, street_width, location, name_prefix)
            all_spawned.extend(sidewalk_results.get("actors", []))
            infrastructure_count += len(sidewalk_results.get("actors", []))
            
            # Urban furniture (benches, trash cans, bus stops)
            furniture_results = _create_urban_furniture(blocks, block_size, location, name_prefix)
            all_spawned.extend(furniture_results.get("actors", []))
            infrastructure_count += len(furniture_results.get("actors", []))
            
            # Parking meters and hydrants
            utility_results = _create_street_utilities(blocks, block_size, location, name_prefix)
            all_spawned.extend(utility_results.get("actors", []))
            infrastructure_count += len(utility_results.get("actors", []))
            
            # Add plaza/square in center for large towns
            if town_size in ["large", "metropolis"]:
                plaza_results = _create_central_plaza(blocks, block_size, location, name_prefix)
                all_spawned.extend(plaza_results.get("actors", []))
                infrastructure_count += len(plaza_results.get("actors", []))
        
        return {
            "success": True,
            "town_stats": {
                "size": town_size,
                "density": building_density,
                "blocks": blocks,
                "buildings": building_count,
                "infrastructure_items": infrastructure_count,
                "total_actors": len(all_spawned),
                "architectural_style": architectural_style
            },
            "actors": all_spawned,
            "message": f"Created {town_size} town with {building_count} buildings and {infrastructure_count} infrastructure items"
        }
        
    except Exception as e:
        logger.error(f"create_town error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_castle_fortress(
    castle_size: str = "large",  # "small", "medium", "large", "epic"
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "Castle",
    include_siege_weapons: bool = True,
    include_village: bool = True,
    architectural_style: str = "medieval"  # "medieval", "fantasy", "gothic"
) -> Dict[str, Any]:
    """
    Create a massive castle fortress with walls, towers, courtyards, throne room,
    and surrounding village. Perfect for dramatic TikTok reveals showing
    the scale and detail of a complete medieval fortress.
    """
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        
        logger.info(f"Creating {castle_size} {architectural_style} castle fortress")
        all_actors = []
        
        # Get size parameters and calculate scaled dimensions
        params = get_castle_size_params(castle_size)
        dimensions = calculate_scaled_dimensions(params, scale_factor=2.0)
        
        # Build castle components using helper functions
        build_outer_bailey_walls(unreal, name_prefix, location, dimensions, all_actors)
        build_inner_bailey_walls(unreal, name_prefix, location, dimensions, all_actors)
        build_gate_complex(unreal, name_prefix, location, dimensions, all_actors)
        build_corner_towers(unreal, name_prefix, location, dimensions, architectural_style, all_actors)
        build_inner_corner_towers(unreal, name_prefix, location, dimensions, all_actors)
        build_intermediate_towers(unreal, name_prefix, location, dimensions, all_actors)
        build_central_keep(unreal, name_prefix, location, dimensions, all_actors)
        build_courtyard_complex(unreal, name_prefix, location, dimensions, all_actors)
        build_bailey_annexes(unreal, name_prefix, location, dimensions, all_actors)
        
        # Add optional components
        if include_siege_weapons:
            build_siege_weapons(unreal, name_prefix, location, dimensions, all_actors)
        
        if include_village:
            build_village_settlement(unreal, name_prefix, location, dimensions, castle_size, all_actors)
        
        # Add final touches
        build_drawbridge_and_moat(unreal, name_prefix, location, dimensions, all_actors)
        add_decorative_flags(unreal, name_prefix, location, dimensions, all_actors)
        
        logger.info(f"Castle fortress creation complete! Created {len(all_actors)} actors")

        
        return {
            "success": True,
            "message": f"Epic {castle_size} {architectural_style} castle fortress created with {len(all_actors)} elements!",
            "actors": all_actors,
            "stats": {
                "size": castle_size,
                "style": architectural_style,
                "wall_sections": int(dimensions["outer_width"]/200) * 2 + int(dimensions["outer_depth"]/200) * 2,
                "towers": dimensions["tower_count"],
                "has_village": include_village,
                "has_siege_weapons": include_siege_weapons,
                "total_actors": len(all_actors)
            }
        }
        
    except Exception as e:
        logger.error(f"create_castle_fortress error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_suspension_bridge(
    span_length: float = 6000.0,
    deck_width: float = 800.0,
    tower_height: float = 4000.0,
    cable_sag_ratio: float = 0.12,
    module_size: float = 200.0,
    location: List[float] = [0.0, 0.0, 0.0],
    orientation: str = "x",
    name_prefix: str = "Bridge",
    deck_mesh: str = "/Engine/BasicShapes/Cube.Cube",
    tower_mesh: str = "/Engine/BasicShapes/Cube.Cube",
    cable_mesh: str = "/Engine/BasicShapes/Cylinder.Cylinder",
    suspender_mesh: str = "/Engine/BasicShapes/Cylinder.Cylinder",
    dry_run: bool = False
) -> Dict[str, Any]:
    """
    Build a suspension bridge with towers, deck, cables, and suspenders.
    
    Creates a realistic suspension bridge with parabolic main cables, vertical
    suspenders, twin towers, and a multi-lane deck. Perfect for dramatic reveals
    showing engineering marvels.
    
    Args:
        span_length: Total span between towers
        deck_width: Width of the bridge deck
        tower_height: Height of support towers
        cable_sag_ratio: Sag as fraction of span (0.1-0.15 typical)
        module_size: Resolution for segments (affects actor count)
        location: Center point of the bridge
        orientation: "x" or "y" for bridge direction
        name_prefix: Prefix for all spawned actors
        deck_mesh: Mesh for deck segments
        tower_mesh: Mesh for tower components
        cable_mesh: Mesh for cable segments
        suspender_mesh: Mesh for vertical suspenders
        dry_run: If True, calculate metrics without spawning
    
    Returns:
        Dictionary with success status, spawned actors, and performance metrics
    """
    try:
        import time
        start_time = time.perf_counter()
        
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        
        logger.info(f"Creating suspension bridge: span={span_length}, width={deck_width}, height={tower_height}")
        
        all_actors = []
        
        # Calculate expected actor counts for dry run
        if dry_run:
            expected_towers = 10  # 2 towers with main, base, top, and 2 attachment points each
            expected_deck = max(1, int(span_length / module_size)) * max(1, int(deck_width / module_size))
            expected_cables = 2 * max(1, int(span_length / module_size))  # 2 main cables
            expected_suspenders = 2 * max(1, int(span_length / (module_size * 3)))  # Every 3 modules
            
            elapsed_ms = int((time.perf_counter() - start_time) * 1000)
            
            return {
                "success": True,
                "dry_run": True,
                "metrics": {
                    "total_actors": expected_towers + expected_deck + expected_cables + expected_suspenders,
                    "deck_segments": expected_deck,
                    "cable_segments": expected_cables,
                    "suspender_count": expected_suspenders,
                    "towers": expected_towers,
                    "span_length": span_length,
                    "deck_width": deck_width,
                    "est_area": span_length * deck_width,
                    "elapsed_ms": elapsed_ms
                }
            }
        
        # Build the bridge structure
        counts = build_suspension_bridge_structure(
            unreal,
            span_length,
            deck_width,
            tower_height,
            cable_sag_ratio,
            module_size,
            location,
            orientation,
            name_prefix,
            deck_mesh,
            tower_mesh,
            cable_mesh,
            suspender_mesh,
            all_actors
        )
        
        # Calculate metrics
        elapsed_ms = int((time.perf_counter() - start_time) * 1000)
        total_actors = sum(counts.values())
        
        logger.info(f"Bridge construction complete: {total_actors} actors in {elapsed_ms}ms")
        
        return {
            "success": True,
            "message": f"Created suspension bridge with {total_actors} components",
            "actors": all_actors,
            "metrics": {
                "total_actors": total_actors,
                "deck_segments": counts["deck_segments"],
                "cable_segments": counts["cable_segments"],
                "suspender_count": counts["suspenders"],
                "towers": counts["towers"],
                "span_length": span_length,
                "deck_width": deck_width,
                "est_area": span_length * deck_width,
                "elapsed_ms": elapsed_ms
            }
        }
        
    except Exception as e:
        logger.error(f"create_suspension_bridge error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_aqueduct(
    arches: int = 18,
    arch_radius: float = 600.0,
    pier_width: float = 200.0,
    tiers: int = 2,
    deck_width: float = 600.0,
    module_size: float = 200.0,
    location: List[float] = [0.0, 0.0, 0.0],
    orientation: str = "x",
    name_prefix: str = "Aqueduct",
    arch_mesh: str = "/Engine/BasicShapes/Cylinder.Cylinder",
    pier_mesh: str = "/Engine/BasicShapes/Cube.Cube",
    deck_mesh: str = "/Engine/BasicShapes/Cube.Cube",
    dry_run: bool = False
) -> Dict[str, Any]:
    """
    Build a multi-tier Roman-style aqueduct with arches and water channel.
    
    Creates a majestic aqueduct with repeating arches, support piers, and
    a water channel deck. Each tier has progressively smaller piers for
    realistic tapering. Perfect for showing ancient engineering.
    
    Args:
        arches: Number of arches per tier
        arch_radius: Radius of each arch
        pier_width: Width of support piers
        tiers: Number of vertical tiers (1-3 recommended)
        deck_width: Width of the water channel
        module_size: Resolution for segments (affects actor count)
        location: Starting point of the aqueduct
        orientation: "x" or "y" for aqueduct direction
        name_prefix: Prefix for all spawned actors
        arch_mesh: Mesh for arch segments (cylinder)
        pier_mesh: Mesh for support piers
        deck_mesh: Mesh for deck and walls
        dry_run: If True, calculate metrics without spawning
    
    Returns:
        Dictionary with success status, spawned actors, and performance metrics
    """
    try:
        import time
        start_time = time.perf_counter()
        
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        
        logger.info(f"Creating aqueduct: {arches} arches, {tiers} tiers, radius={arch_radius}")
        
        all_actors = []
        
        # Calculate dimensions
        total_length = arches * (2 * arch_radius + pier_width) + pier_width
        
        # Calculate expected actor counts for dry run
        if dry_run:
            # Arch segments per arch based on semicircle circumference
            arch_circumference = math.pi * arch_radius
            segments_per_arch = max(4, int(arch_circumference / module_size))
            expected_arch_segments = tiers * arches * segments_per_arch
            
            # Piers: (arches + 1) per tier
            expected_piers = tiers * (arches + 1)
            
            # Deck segments including side walls
            deck_length_segments = max(1, int(total_length / module_size))
            deck_width_segments = max(1, int(deck_width / module_size))
            expected_deck = deck_length_segments * deck_width_segments
            expected_deck += 2 * deck_length_segments  # Side walls
            
            elapsed_ms = int((time.perf_counter() - start_time) * 1000)
            
            return {
                "success": True,
                "dry_run": True,
                "metrics": {
                    "total_actors": expected_arch_segments + expected_piers + expected_deck,
                    "arch_segments": expected_arch_segments,
                    "pier_count": expected_piers,
                    "tiers": tiers,
                    "deck_segments": expected_deck,
                    "total_length": total_length,
                    "est_area": total_length * deck_width,
                    "elapsed_ms": elapsed_ms
                }
            }
        
        # Build the aqueduct structure
        counts = build_aqueduct_structure(
            unreal,
            arches,
            arch_radius,
            pier_width,
            tiers,
            deck_width,
            module_size,
            location,
            orientation,
            name_prefix,
            arch_mesh,
            pier_mesh,
            deck_mesh,
            all_actors
        )
        
        # Calculate metrics
        elapsed_ms = int((time.perf_counter() - start_time) * 1000)
        total_actors = sum(counts.values())
        
        logger.info(f"Aqueduct construction complete: {total_actors} actors in {elapsed_ms}ms")
        
        return {
            "success": True,
            "message": f"Created {tiers}-tier aqueduct with {arches} arches ({total_actors} components)",
            "actors": all_actors,
            "metrics": {
                "total_actors": total_actors,
                "arch_segments": counts["arch_segments"],
                "pier_count": counts["piers"],
                "tiers": tiers,
                "deck_segments": counts["deck_segments"],
                "total_length": total_length,
                "est_area": total_length * deck_width,
                "elapsed_ms": elapsed_ms
            }
        }
        
    except Exception as e:
        logger.error(f"create_aqueduct error: {e}")
        return {"success": False, "message": str(e)}



# ============================================================================
# Blueprint Node Graph Tool
# ============================================================================

@mcp.tool()
def add_node(
    blueprint_name: str,
    node_type: str,
    pos_x: float = 0,
    pos_y: float = 0,
    message: str = "",
    event_type: str = "BeginPlay",
    variable_name: str = "",
    target_function: str = "",
    target_blueprint: Optional[str] = None,
    function_name: Optional[str] = None
) -> Dict[str, Any]:
    """
    Add a node to a Blueprint graph.

    Create various types of K2Nodes in a Blueprint's event graph or function graph.
    Supports 23 node types organized by category.

    Args:
        blueprint_name: Name of the Blueprint to modify
        node_type: Type of node to create. Supported types (23 total):

            CONTROL FLOW:
                "Branch" - Conditional execution (if/then/else)
                "Comparison" - Arithmetic/logical operators (==, !=, <, >, AND, OR, etc.)
                    ℹ️ Types can be changed via set_node_property with action="set_pin_type"
                "Switch" - Switch on byte/enum value with cases
                    ℹ️ Creates 1 pin at creation; add more via set_node_property with action="add_pin"
                "SwitchEnum" - Switch on enum type (auto-generates pins per enum value)
                    ℹ️ Creates pins based on enum; change enum via set_node_property with action="set_enum_type"
                "SwitchInteger" - Switch on integer value with cases
                    ℹ️ Creates 1 pin at creation; add more via set_node_property with action="add_pin"
                "ExecutionSequence" - Sequential execution with multiple outputs
                    ℹ️ Creates 1 pin at creation; add/remove via set_node_property (add_pin/remove_pin)

            DATA:
                "VariableGet" - Read a variable value (⚠️ variable must exist in Blueprint)
                "VariableSet" - Set a variable value (⚠️ variable must exist and be assignable)
                "MakeArray" - Create array from individual inputs
                    ℹ️ Creates 1 pin at creation; add/remove via set_node_property with action="set_num_elements"

            CASTING:
                "DynamicCast" - Cast object to specific class (⚠️ handle "Cast Failed" output)
                "ClassDynamicCast" - Cast class reference to derived class (⚠️ handle failure cases)
                "CastByteToEnum" - Convert byte value to enum (⚠️ byte must be valid enum range)

            UTILITY:
                "Print" - Debug output to screen/log (configurable duration and color)
                "CallFunction" - Call any blueprint/engine function (⚠️ function must exist)
                "Select" - Choose between two inputs based on boolean condition
                "SpawnActor" - Spawn actor from class (⚠️ class must derive from Actor)

            SPECIALIZED:
                "Timeline" - Animation timeline playback with curve tracks
                    ⚠️ REQUIRES MANUAL IMPLEMENTATION: Animation curves must be added in editor
                "GetDataTableRow" - Query row from data table (⚠️ DataTable must exist)
                "AddComponentByClass" - Dynamically add component to actor
                "Self" - Reference to current actor/object
                "Knot" - Invisible reroute node (wire organization only)

            EVENT:
                "Event" - Blueprint event (specify event_type: BeginPlay, Tick, etc.)
                    ℹ️ Tick events run every frame - be mindful of performance impact

        pos_x: X position in graph (default: 0)
        pos_y: Y position in graph (default: 0)
        message: For Print nodes, the text to print
        event_type: For Event nodes, the event name (BeginPlay, Tick, Destroyed, etc.)
        variable_name: For Variable nodes, the variable name
        target_function: For CallFunction nodes, the function to call
        target_blueprint: For CallFunction nodes, optional path to target Blueprint
        function_name: Optional name of function graph to add node to (if None, uses EventGraph)

    Returns:
        Dictionary with success status, node_id, and position

    Important Notes:
        - Most nodes can have pins modified after creation via set_node_property
        - Dynamic pin management: Switch/SwitchEnum/ExecutionSequence/MakeArray support pin operations
        - Timeline is the ONLY node requiring manual implementation (curves must be added in editor)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        node_params = {
            "pos_x": pos_x,
            "pos_y": pos_y
        }

        if message:
            node_params["message"] = message
        if event_type:
            node_params["event_type"] = event_type
        if variable_name:
            node_params["variable_name"] = variable_name
        if target_function:
            node_params["target_function"] = target_function
        if target_blueprint:
            node_params["target_blueprint"] = target_blueprint
        if function_name:
            node_params["function_name"] = function_name

        result = node_manager.add_node(
            unreal,
            blueprint_name,
            node_type,
            node_params
        )

        return result

    except Exception as e:
        logger.error(f"add_node error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def connect_nodes(
    blueprint_name: str,
    source_node_id: str,
    source_pin_name: str,
    target_node_id: str,
    target_pin_name: str,
    function_name: Optional[str] = None
) -> Dict[str, Any]:
    """
    Connect two nodes in a Blueprint graph.

    Links a source pin to a target pin between existing nodes in a Blueprint's event graph or function graph.

    Args:
        blueprint_name: Name of the Blueprint to modify
        source_node_id: ID of the source node
        source_pin_name: Name of the output pin on the source node
        target_node_id: ID of the target node
        target_pin_name: Name of the input pin on the target node
        function_name: Optional name of function graph (if None, uses EventGraph)

    Returns:
        Dictionary with success status and connection details
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = connector_manager.connect_nodes(
            unreal,
            blueprint_name,
            source_node_id,
            source_pin_name,
            target_node_id,
            target_pin_name,
            function_name
        )

        return result
    except Exception as e:
        logger.error(f"connect_nodes error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def create_variable(
    blueprint_name: str,
    variable_name: str,
    variable_type: str,
    default_value: Any = None,
    is_public: bool = False,
    tooltip: str = "",
    category: str = "Default"
) -> Dict[str, Any]:
    """
    Create a variable in a Blueprint.

    Adds a new variable to a Blueprint with specified type, default value, and properties.

    Args:
        blueprint_name: Name of the Blueprint to modify
        variable_name: Name of the variable to create
        variable_type: Type of the variable ("bool", "int", "float", "string", "vector", "rotator")
        default_value: Default value for the variable (optional)
        is_public: Whether the variable should be public/editable (default: False)
        tooltip: Tooltip text for the variable (optional)
        category: Category for organizing variables (default: "Default")

    Returns:
        Dictionary with success status and variable details
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = variable_manager.create_variable(
            unreal,
            blueprint_name,
            variable_name,
            variable_type,
            default_value,
            is_public,
            tooltip,
            category
        )

        return result
    except Exception as e:
        logger.error(f"create_variable error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_blueprint_variable_properties(
    blueprint_name: str,
    variable_name: str,
    var_name: Optional[str] = None,
    var_type: Optional[str] = None,
    is_blueprint_readable: Optional[bool] = None,
    is_blueprint_writable: Optional[bool] = None,
    is_public: Optional[bool] = None,
    is_editable_in_instance: Optional[bool] = None,
    tooltip: Optional[str] = None,
    category: Optional[str] = None,
    default_value: Any = None,
    expose_on_spawn: Optional[bool] = None,
    expose_to_cinematics: Optional[bool] = None,
    slider_range_min: Optional[str] = None,
    slider_range_max: Optional[str] = None,
    value_range_min: Optional[str] = None,
    value_range_max: Optional[str] = None,
    units: Optional[str] = None,
    bitmask: Optional[bool] = None,
    bitmask_enum: Optional[str] = None,
    replication_enabled: Optional[bool] = None,
    replication_condition: Optional[int] = None,
    is_private: Optional[bool] = None
) -> Dict[str, Any]:
    """
    Modify properties of an existing Blueprint variable without deleting it.

    Preserves all VariableGet and VariableSet nodes connected to this variable.

    Args:
        blueprint_name: Name of the Blueprint to modify
        variable_name: Name of the variable to modify

        var_name: Rename the variable (optional)
            ✅ PASS - VarDesc->VarName works correctly

        var_type: Change variable type (optional)
            ✅ PASS - VarDesc->VarType works correctly (int→float returns "real")

        is_blueprint_readable: Allow reading in Blueprint (VariableGet) (optional)
            ✅ PASS - CPF_BlueprintReadOnly flag (inverted logic)

        is_blueprint_writable: Allow writing in Blueprint (Set) (optional)
            ✅ PASS - CPF_BlueprintReadOnly flag (inverted logic)
            ⚠️ NOT returned by get_variable_details()

        is_public: Visible in Blueprint editor (optional)
            ✅ PASS - Controls variable visibility

        is_editable_in_instance: Modifiable on instances (optional)
            ✅ PASS - CPF_DisableEditOnInstance flag (inverted logic)

        tooltip: Variable description (optional)
            ✅ PASS - Metadata MD_Tooltip works correctly

        category: Variable category (optional)
            ✅ PASS - Direct property Category works

        default_value: New default value (optional)
            ✅ PASS - Works but get_variable_details() returns empty string

        expose_on_spawn: Show in spawn dialog (optional)
            ✅ PASS - Metadata MD_ExposeOnSpawn works
            ⚠️ Requires is_editable_in_instance=true to be visible
            ⚠️ NOT returned by get_variable_details()

        expose_to_cinematics: Expose to cinematics (optional)
            ✅ PASS - CPF_Interp flag works correctly
            ⚠️ NOT returned by get_variable_details()

        slider_range_min: UI slider minimum value (optional)
            ✅ PASS - Metadata MD_UIMin works (string value)
            ⚠️ NOT returned by get_variable_details()

        slider_range_max: UI slider maximum value (optional)
            ✅ PASS - Metadata MD_UIMax works (string value)
            ⚠️ NOT returned by get_variable_details()

        value_range_min: Clamp minimum value (optional)
            ✅ PASS - Metadata MD_ClampMin works (string value)
            ⚠️ NOT returned by get_variable_details()

        value_range_max: Clamp maximum value (optional)
            ✅ PASS - Metadata MD_ClampMax works (string value)
            ⚠️ NOT returned by get_variable_details()

        units: Display units (optional)
            ⚠️ PARTIAL - Metadata MD_Units works for value display (e.g., "0.0 cm")
            ❌ UI dropdown stays at "None" (Unreal Editor limitation - dropdown doesn't sync with metadata)
            ⚠️ Use long format: "Centimeters", "Meters" (not "cm", "m")
            ⚠️ NOT returned by get_variable_details()

        bitmask: Treat as bitmask (optional)
            ✅ PASS - Metadata TEXT("Bitmask") works correctly
            ⚠️ NOT returned by get_variable_details()

        bitmask_enum: Bitmask enum type (optional)
            ✅ PASS - Metadata TEXT("BitmaskEnum") works
            ⚠️ REQUIRES full path format: "/Script/ModuleName.EnumName"
            ❌ Short names generate warning and don't sync dropdown
            ✅ Validated enums (use FULL PATHS):
                - /Script/UniversalObjectLocator.ELocatorResolveFlags
                - /Script/JsonObjectGraph.EJsonStringifyFlags
                - /Script/MediaAssets.EMediaAudioCaptureDeviceFilter
                - /Script/MediaAssets.EMediaVideoCaptureDeviceFilter
                - /Script/MediaAssets.EMediaWebcamCaptureDeviceFilter
                - /Script/Engine.EAnimAssetCurveFlags
                - /Script/Engine.EHardwareDeviceSupportedFeatures
                - /Script/EnhancedInput.EMappingQueryIssue
                - /Script/EnhancedInput.ETriggerEvent
            ⚠️ NOT returned by get_variable_details()

        replication_enabled: Enable network replication (CPF_Net flag) (optional)
            ✅ PASS - CPF_Net flag works - Changes "Replication" dropdown (None ↔ Replicated)
            ⚠️ NOT returned by get_variable_details()

        replication_condition: Network replication condition (ELifetimeCondition 0-7) (optional)
            ✅ PASS - VarDesc->ReplicationCondition works
            ✅ Changes "Replication Condition" dropdown (e.g., None → Initial Only)
            ⚠️ Values: 0=None, 1=InitialOnly, 2=OwnerOnly, 3=SkipOwner, 4=SimulatedOnly, 5=AutonomousOnly, 6=SimulatedOrPhysics, 7=InitialOrOwner
            ✅ Returned by get_variable_details() as "replication"

        is_private: Set variable as private (optional)
            ❌ UNRESOLVED - Property flag/metadata not yet identified
            ⚠️ Attempted CPF_NativeAccessSpecifierPrivate flag and MD_AllowPrivateAccess metadata - neither work
            ⚠️ The property that controls "Privé" (Private) checkbox remains unknown
            ⚠️ Parameter exists but has no effect on UI - do NOT use until resolved

    Returns:
        Dictionary with success status and updated properties
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = variable_manager.set_blueprint_variable_properties(
            unreal,
            blueprint_name,
            variable_name,
            var_name,
            var_type,
            is_blueprint_readable,
            is_blueprint_writable,
            is_public,
            is_editable_in_instance,
            tooltip,
            category,
            default_value,
            expose_on_spawn,
            expose_to_cinematics,
            slider_range_min,
            slider_range_max,
            value_range_min,
            value_range_max,
            units,
            bitmask,
            bitmask_enum,
            replication_enabled,
            replication_condition,
            is_private
        )

        return result
    except Exception as e:
        logger.error(f"set_blueprint_variable_properties error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def add_event_node(
    blueprint_name: str,
    event_name: str,
    pos_x: float = 0,
    pos_y: float = 0
) -> Dict[str, Any]:
    """
    Add an event node to a Blueprint graph.

    Create specialized event nodes (ReceiveBeginPlay, ReceiveTick, etc.)
    in a Blueprint's event graph at specified positions.

    Args:
        blueprint_name: Name of the Blueprint to modify
        event_name: Name of the event (e.g., "ReceiveBeginPlay", "ReceiveTick", "ReceiveDestroyed")
        pos_x: X position in graph (default: 0)
        pos_y: Y position in graph (default: 0)

    Returns:
        Dictionary with success status, node_id, event_name, and position
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = event_manager.add_event_node(
            unreal,
            blueprint_name,
            event_name,
            pos_x,
            pos_y
        )

        return result
    except Exception as e:
        logger.error(f"add_event_node error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_node(
    blueprint_name: str,
    node_id: str,
    function_name: Optional[str] = None
) -> Dict[str, Any]:
    """
    Delete a node from a Blueprint graph.

    Removes a node and all its connections from either the EventGraph
    or a specific function graph.

    Args:
        blueprint_name: Name of the Blueprint to modify
        node_id: ID of the node to delete (NodeGuid or node name)
        function_name: Name of function graph (optional, defaults to EventGraph)

    Returns:
        Dictionary with success status and deleted_node_id
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = node_deleter.delete_node(
            unreal,
            blueprint_name,
            node_id,
            function_name
        )
        return result
    except Exception as e:
        logger.error(f"delete_node error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_node_property(
    blueprint_name: str,
    node_id: str,
    property_name: str = "",
    property_value: Any = None,
    function_name: Optional[str] = None,
    action: Optional[str] = None,
    pin_type: Optional[str] = None,
    pin_name: Optional[str] = None,
    enum_type: Optional[str] = None,
    new_type: Optional[str] = None,
    target_type: Optional[str] = None,
    target_function: Optional[str] = None,
    target_class: Optional[str] = None,
    event_type: Optional[str] = None
) -> Dict[str, Any]:
    """
    Set a property on a Blueprint node or perform semantic node editing.

    This function supports both simple property modifications and advanced semantic
    node editing operations (pin management, type modifications, reference updates).

    Args:
        blueprint_name: Name of the Blueprint to modify
        node_id: ID of the node to modify
        property_name: Name of property to set (legacy mode, used if action not specified)
        property_value: Value to set (legacy mode)
        function_name: Name of function graph (optional, defaults to EventGraph)
        action: Semantic action to perform - can be one of:
            Phase 1 (Pin Management):
                - "add_pin": Add a pin to a node (requires pin_type)
                - "remove_pin": Remove a pin from a node (requires pin_name)
                - "set_enum_type": Set enum type on a node (requires enum_type)
            Phase 2 (Type Modification):
                - "set_pin_type": Change pin type on comparison nodes (requires pin_name, new_type)
                - "set_value_type": Change value type on select nodes (requires new_type)
                - "set_cast_target": Change cast target type (requires target_type)
            Phase 3 (Reference Updates - DESTRUCTIVE):
                - "set_function_call": Change function being called (requires target_function)
                - "set_event_type": Change event type (requires event_type)

    Semantic action parameters:
        pin_type: Type of pin to add ("SwitchCase", "ExecutionOutput", "ArrayElement", "EnumValue")
        pin_name: Name of pin to remove or modify
        enum_type: Full path to enum type (e.g., "/Game/Enums/ECardinalDirection")
        new_type: New type for pin or value ("int", "float", "string", "bool", "vector", etc.)
        target_type: Target class path for casting
        target_function: Name of function to call
        target_class: Optional class containing the function
        event_type: Event type (e.g., "BeginPlay", "Tick", "Destroyed")

    Returns:
        Dictionary with success status and details

    Supported legacy properties by node type:
        - Print nodes: "message", "duration", "text_color"
        - Variable nodes: "variable_name"
        - All nodes: "pos_x", "pos_y", "comment"

    Examples:
        Legacy mode (set simple property):
            set_node_property(
                blueprint_name="MyActorBlueprint",
                node_id="K2Node_1234567890",
                property_name="message",
                property_value="Hello World!"
            )

        Semantic mode (add pin):
            set_node_property(
                blueprint_name="MyActorBlueprint",
                node_id="K2Node_Switch_123",
                action="add_pin",
                pin_type="SwitchCase"
            )

        Semantic mode (set enum type):
            set_node_property(
                blueprint_name="MyActorBlueprint",
                node_id="K2Node_SwitchEnum_456",
                action="set_enum_type",
                enum_type="ECardinalDirection"
            )

        Semantic mode (change function call):
            set_node_property(
                blueprint_name="MyActorBlueprint",
                node_id="K2Node_CallFunction_789",
                action="set_function_call",
                target_function="BeginPlay",
                target_class="APawn"
            )
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        # Build kwargs for semantic actions
        kwargs = {}
        if action is not None:
            if pin_type is not None:
                kwargs["pin_type"] = pin_type
            if pin_name is not None:
                kwargs["pin_name"] = pin_name
            if enum_type is not None:
                kwargs["enum_type"] = enum_type
            if new_type is not None:
                kwargs["new_type"] = new_type
            if target_type is not None:
                kwargs["target_type"] = target_type
            if target_function is not None:
                kwargs["target_function"] = target_function
            if target_class is not None:
                kwargs["target_class"] = target_class
            if event_type is not None:
                kwargs["event_type"] = event_type

        result = node_properties.set_node_property(
            unreal,
            blueprint_name,
            node_id,
            property_name,
            property_value,
            function_name,
            action,
            **kwargs
        )
        return result
    except Exception as e:
        logger.error(f"set_node_property error: {e}", exc_info=True)
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_function(
    blueprint_name: str,
    function_name: str,
    return_type: str = "void"
) -> Dict[str, Any]:
    """
    Create a new function in a Blueprint.

    Args:
        blueprint_name: Name of the Blueprint to modify
        function_name: Name for the new function
        return_type: Return type of the function (default: "void")

    Returns:
        Dictionary with function_name, graph_id or error
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = function_manager.create_function_handler(
            unreal,
            blueprint_name,
            function_name,
            return_type
        )
        return result
    except Exception as e:
        logger.error(f"create_function error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_function_input(
    blueprint_name: str,
    function_name: str,
    param_name: str,
    param_type: str,
    is_array: bool = False
) -> Dict[str, Any]:
    """
    Add an input parameter to a Blueprint function.

    Args:
        blueprint_name: Name of the Blueprint to modify
        function_name: Name of the function
        param_name: Name of the input parameter
        param_type: Type of the parameter (bool, int, float, string, vector, etc.)
        is_array: Whether the parameter is an array (default: False)

    Returns:
        Dictionary with param_name, param_type, and direction or error
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = function_io.add_function_input_handler(
            unreal,
            blueprint_name,
            function_name,
            param_name,
            param_type,
            is_array
        )
        return result
    except Exception as e:
        logger.error(f"add_function_input error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_function_output(
    blueprint_name: str,
    function_name: str,
    param_name: str,
    param_type: str,
    is_array: bool = False
) -> Dict[str, Any]:
    """
    Add an output parameter to a Blueprint function.

    Args:
        blueprint_name: Name of the Blueprint to modify
        function_name: Name of the function
        param_name: Name of the output parameter
        param_type: Type of the parameter (bool, int, float, string, vector, etc.)
        is_array: Whether the parameter is an array (default: False)

    Returns:
        Dictionary with param_name, param_type, and direction or error
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = function_io.add_function_output_handler(
            unreal,
            blueprint_name,
            function_name,
            param_name,
            param_type,
            is_array
        )
        return result
    except Exception as e:
        logger.error(f"add_function_output error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_function(
    blueprint_name: str,
    function_name: str
) -> Dict[str, Any]:
    """
    Delete a function from a Blueprint.

    Args:
        blueprint_name: Name of the Blueprint to modify
        function_name: Name of the function to delete

    Returns:
        Dictionary with deleted_function_name or error
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = function_manager.delete_function_handler(
            unreal,
            blueprint_name,
            function_name
        )
        return result
    except Exception as e:
        logger.error(f"delete_function error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_function(
    blueprint_name: str,
    old_function_name: str,
    new_function_name: str
) -> Dict[str, Any]:
    """
    Rename a function in a Blueprint.

    Args:
        blueprint_name: Name of the Blueprint to modify
        old_function_name: Current name of the function
        new_function_name: New name for the function

    Returns:
        Dictionary with new_function_name or error
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        result = function_manager.rename_function_handler(
            unreal,
            blueprint_name,
            old_function_name,
            new_function_name
        )
        return result
    except Exception as e:
        logger.error(f"rename_function error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Kenshi-Style Medieval Settlement Generator
# ============================================================================

@mcp.tool()
def create_kenshi_settlement(
    settlement_type: str = "town",
    faction_style: str = "neutral",
    location: List[float] = [0.0, 0.0, 0.0],
    name_prefix: str = "Settlement",
    irregularity: float = 0.3,
    seed: int = 0,
    export_json: bool = True,
) -> Dict[str, Any]:
    """
    Create a Kenshi-style medieval settlement with organic layout.

    Generates an organic medieval town with irregular walls, gates, roads,
    district-zoned buildings, guard posts, and patrol routes. Previews as
    colored primitive actors in the viewport and exports JSON for PRK
    DataAsset import.

    Settlement types:
    - outpost: Small palisade, 3-5 buildings, 1 gate
    - village: Wood walls, 8-15 buildings, 1-2 gates
    - town: Stone walls, 20-35 buildings, 2-3 gates, districts
    - city: Large stone walls, 40-60 buildings, 3-4 gates, many districts
    - fortress: Thick walls, 15-25 buildings, military focus

    Args:
        settlement_type: outpost, village, town, city, fortress
        faction_style: neutral, holy_nation, shek, tech (affects future layout styles)
        location: [x, y, z] world position for settlement center
        name_prefix: Prefix for all spawned actors
        irregularity: 0.0=regular polygon walls, 1.0=very irregular organic walls
        seed: Random seed for reproducible layouts (0=random)
        export_json: If True, export layout JSON to Saved/SettlementLayouts/
    """
    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}

        logger.info(f"Creating {settlement_type} settlement at {location} (seed={seed})")

        result = _create_kenshi_settlement(
            unreal=unreal,
            settlement_type=settlement_type,
            faction_style=faction_style,
            location=location,
            name_prefix=name_prefix,
            irregularity=irregularity,
            seed=seed,
            export_json=export_json,
        )

        return result

    except Exception as e:
        logger.error(f"create_kenshi_settlement error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Thumbnail Generation Tools
# ============================================================================

@mcp.tool()
def generate_thumbnail(
    asset_path: str,
    resolution: int = 256,
    transparent: bool = True,
    camera_fov: float = 30.0,
    camera_pitch: float = -15.0,
    camera_yaw: float = 90.0,
    save_directory: str = "/Game/PRK/UI/Icons",
    ambient_light_only: bool = False,
    export_png: bool = False,
    export_disk_path: str = ""
) -> Dict[str, Any]:
    """
    Generate a UI icon thumbnail for a single asset.

    Renders the asset's mesh using an offscreen SceneCaptureComponent2D and
    saves it as a UTexture2D.

    Supports:
    - UStaticMesh / USkeletalMesh paths (rendered directly)
    - UPRKWeaponData / UPRKArmorData paths (extracts EquipStaticMesh/EquipSkeletalMesh)
    - UPRKItemData paths (uses WorldMesh)

    Args:
        asset_path: Package path (e.g. "/Game/PRK/Items/Meshes/Barbarian/Male/Helm/SK_helm")
        resolution: Output texture size in pixels (default 256, max 2048)
        transparent: If true, render with transparent background (alpha channel)
        camera_fov: Camera field of view in degrees (default 30)
        camera_pitch: Camera pitch angle (default -15, negative = looking down)
        camera_yaw: Camera yaw angle (default 90, front view for Y-facing meshes)
        save_directory: Package path for output (default "/Game/PRK/UI/Icons")
        ambient_light_only: If true, skip three-point directional lights and use scene ambient only
        export_png: If true, also save a PNG file to disk at export_disk_path
        export_disk_path: Disk folder for PNG export (e.g. "E:/Output/Icons/")

    Returns:
        Dict with saved_path of the generated texture (and export_png_path if exported)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {
            "asset_path": asset_path,
            "resolution": resolution,
            "transparent": transparent,
            "camera_fov": camera_fov,
            "camera_pitch": camera_pitch,
            "camera_yaw": camera_yaw,
            "save_directory": save_directory,
            "ambient_light_only": ambient_light_only,
            "export_png": export_png,
            "export_disk_path": export_disk_path
        }
        response = unreal.send_command("generate_thumbnail", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"generate_thumbnail error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def generate_thumbnails(
    asset_paths: Optional[List[str]] = None,
    directories: Optional[List[str]] = None,
    resolution: int = 256,
    transparent: bool = True,
    camera_fov: float = 30.0,
    camera_pitch: float = -15.0,
    camera_yaw: float = 90.0,
    save_directory: str = "/Game/PRK/UI/Icons",
    ambient_light_only: bool = False,
    export_png: bool = False,
    export_disk_path: str = ""
) -> Dict[str, Any]:
    """
    Batch generate UI icon thumbnails for multiple assets or entire directories.

    Accepts explicit asset paths AND/OR directories to scan recursively.
    Directories are scanned for UStaticMesh and USkeletalMesh assets.

    Args:
        asset_paths: List of individual asset package paths
        directories: List of directory package paths to scan recursively
        resolution: Output texture size in pixels (default 256, max 2048)
        transparent: If true, render with transparent background
        camera_fov: Camera field of view in degrees
        camera_pitch: Camera pitch angle
        camera_yaw: Camera yaw angle
        save_directory: Package path for output
        ambient_light_only: If true, skip three-point directional lights and use scene ambient only
        export_png: If true, also save PNG files to disk at export_disk_path
        export_disk_path: Disk folder for PNG export (e.g. "E:/Output/Icons/")

    Returns:
        Dict with total/succeeded/failed counts and per-asset results
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {
            "resolution": resolution,
            "transparent": transparent,
            "camera_fov": camera_fov,
            "camera_pitch": camera_pitch,
            "camera_yaw": camera_yaw,
            "save_directory": save_directory,
            "ambient_light_only": ambient_light_only,
            "export_png": export_png,
            "export_disk_path": export_disk_path
        }
        if asset_paths:
            params["asset_paths"] = asset_paths
        if directories:
            params["directories"] = directories

        response = unreal.send_command("generate_thumbnails", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"generate_thumbnails error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def scan_assets_for_thumbnails(
    directories: List[str],
    include_static: bool = True,
    include_skeletal: bool = True
) -> Dict[str, Any]:
    """
    Scan directories for mesh assets that can have thumbnails generated.

    Dry-run tool: returns the list of found mesh paths without generating anything.
    Use this to preview what generate_thumbnails would process.

    Args:
        directories: List of directory package paths to scan (e.g. ["/Game/PRK/Items/Meshes/"])
        include_static: Include UStaticMesh assets (default true)
        include_skeletal: Include USkeletalMesh assets (default true)

    Returns:
        Dict with count and list of found asset_paths
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {
            "directories": directories,
            "include_static": include_static,
            "include_skeletal": include_skeletal
        }
        response = unreal.send_command("scan_assets_for_thumbnails", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"scan_assets_for_thumbnails error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Material Analysis Tools
# ============================================================================

@mcp.tool()
def analyze_material(
    material_path: str
) -> Dict[str, Any]:
    """
    Analyze a base UMaterial's expression graph — returns all nodes, their connections, and properties.

    Use this to understand how a material is constructed: what nodes it uses, how they're
    connected, parameter names/defaults, texture references, function calls, and custom HLSL code.

    Args:
        material_path: Package path to the material (e.g. "/Game/Materials/M_MyMaterial")

    Returns:
        Dict with expressions array containing type, inputs, outputs, and specialized properties for each node
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_material_expressions", {"material_path": material_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_material error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_material_output_connections(
    material_path: str
) -> Dict[str, Any]:
    """
    Get what feeds each material output pin (BaseColor, Normal, Roughness, etc.).

    Quick overview of a material's output wiring without needing the full expression graph.
    Shows which expression node is connected to each material property.

    Args:
        material_path: Package path to the material (e.g. "/Game/Materials/M_MyMaterial")

    Returns:
        Dict with connections array (all outputs) and connected_outputs (only wired ones)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_material_connections", {"material_path": material_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_output_connections error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def analyze_material_function(
    function_path: str
) -> Dict[str, Any]:
    """
    Analyze a UMaterialFunction — returns inputs, outputs, description, and internal expression graph.

    Use this to understand reusable material function nodes: what they accept as input,
    what they output, and how the internal graph transforms the data.

    Args:
        function_path: Package path to the material function (e.g. "/Engine/Functions/Engine_MaterialFunctions02/Texturing/FlattenNormal")

    Returns:
        Dict with function metadata, inputs, outputs, and internal expressions
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_material_function_info", {"function_path": function_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_material_function error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_material_stats(
    material_path: str
) -> Dict[str, Any]:
    """
    Get material properties and statistics — domain, blend mode, shading model, expression counts, parameter counts.

    Quick summary of a material's configuration without the full expression graph.
    Useful for understanding material type and complexity at a glance.

    Args:
        material_path: Package path to the material (e.g. "/Game/Materials/M_MyMaterial")

    Returns:
        Dict with domain, blend_mode, shading_models, two_sided, expression_count, parameter_counts, etc.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_material_stats", {"material_path": material_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_stats error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Level Analysis Tools
# ============================================================================

@mcp.tool()
def get_rvt_volumes() -> Dict[str, Any]:
    """
    Get all RuntimeVirtualTextureVolume actors in the current editor level.

    Returns each volume's name, transform, bounds, component settings (hide primitives,
    snap to landscape, bounds align actor, expand bounds, stream low mips), and the
    assigned RVT asset info (material type, tile count, tile size, size, layer count,
    compression, adaptive, continuous update).

    Returns:
        Dict with count and volumes array containing full RVT volume details
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_rvt_volumes", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_rvt_volumes error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_landscape_info() -> Dict[str, Any]:
    """
    Get all Landscape and LandscapeProxy actors in the current editor level.

    Returns each landscape's name, class, transform, bounds, grid dimensions
    (component size quads, subsection size quads, num subsections, component count),
    LOD settings, Nanite state, material paths, virtual texture settings
    (assigned RVTs with material type/size/layers, VT LODs, quad rendering,
    main pass type), physical material, bounds extensions, and streaming multiplier.

    Returns:
        Dict with count and landscapes array containing full landscape details
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_landscape_info", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_landscape_info error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Runtime Virtual Texture Tools
# ============================================================================

@mcp.tool()
def get_rvt_info(
    asset_path: str
) -> Dict[str, Any]:
    """
    Get all properties of a RuntimeVirtualTexture asset.

    Returns tile counts (raw setting + computed actual), tile size, border size,
    material type, compression settings, page table flags, custom material data,
    LOD group, layer count, computed total size, and priority.

    Args:
        asset_path: Package path to the RVT asset (e.g. "/Game/VirtualTextures/RVT_Roads")

    Returns:
        Dict with all RVT configuration fields and computed values
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("get_rvt_info", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_rvt_info error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_rvt_properties(
    asset_path: str,
    tile_count: Optional[int] = None,
    tile_size: Optional[int] = None,
    tile_border_size: Optional[int] = None,
    material_type: Optional[str] = None,
    compress_textures: Optional[bool] = None,
    use_low_quality_compression: Optional[bool] = None,
    clear_textures: Optional[bool] = None,
    single_physical_space: Optional[bool] = None,
    private_space: Optional[bool] = None,
    adaptive: Optional[bool] = None,
    continuous_update: Optional[bool] = None,
    remove_low_mips: Optional[int] = None,
    custom_material_data: Optional[List[float]] = None,
    lod_group: Optional[str] = None,
    custom_priority: Optional[str] = None,
    use_custom_priority: Optional[bool] = None
) -> Dict[str, Any]:
    """
    Modify properties on a RuntimeVirtualTexture asset.

    Only provided (non-None) fields are changed. After setting, triggers
    PostEditChangeProperty and marks the package dirty for save.

    Args:
        asset_path: Package path to the RVT asset
        tile_count: Tile count exponent (0-12, actual = 2^value)
        tile_size: Tile size exponent (0-4, actual = 2^(value+6))
        tile_border_size: Border size (0-4, actual = 2*value)
        material_type: One of: BaseColor, Mask4, BaseColor_Normal_Roughness,
                       BaseColor_Normal_Specular, BaseColor_Normal_Specular_YCoCg,
                       BaseColor_Normal_Specular_Mask_YCoCg, WorldHeight, Displacement
        compress_textures: Enable GPU compression
        use_low_quality_compression: Use low quality compression (RGB565)
        clear_textures: Clear before rendering
        single_physical_space: Enable packed page table
        private_space: Enable private page table
        adaptive: Enable sparse adaptive page tables
        continuous_update: Enable continuous page updates
        remove_low_mips: Number of low mips to remove (0-5)
        custom_material_data: Float4 custom data as [x, y, z, w]
        lod_group: Texture group name (e.g. "TEXTUREGROUP_World")
        custom_priority: Priority string: Lowest/Lower/Low/BelowNormal/Normal/AboveNormal/High/Highest
        use_custom_priority: Whether to use custom priority

    Returns:
        Dict with fields_set list and any errors
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    # Build properties dict from non-None args
    properties = {}
    if tile_count is not None:
        properties["tile_count"] = tile_count
    if tile_size is not None:
        properties["tile_size"] = tile_size
    if tile_border_size is not None:
        properties["tile_border_size"] = tile_border_size
    if material_type is not None:
        properties["material_type"] = material_type
    if compress_textures is not None:
        properties["compress_textures"] = compress_textures
    if use_low_quality_compression is not None:
        properties["use_low_quality_compression"] = use_low_quality_compression
    if clear_textures is not None:
        properties["clear_textures"] = clear_textures
    if single_physical_space is not None:
        properties["single_physical_space"] = single_physical_space
    if private_space is not None:
        properties["private_space"] = private_space
    if adaptive is not None:
        properties["adaptive"] = adaptive
    if continuous_update is not None:
        properties["continuous_update"] = continuous_update
    if remove_low_mips is not None:
        properties["remove_low_mips"] = remove_low_mips
    if custom_material_data is not None:
        properties["custom_material_data"] = custom_material_data
    if lod_group is not None:
        properties["lod_group"] = lod_group
    if custom_priority is not None:
        properties["custom_priority"] = custom_priority
    if use_custom_priority is not None:
        properties["use_custom_priority"] = use_custom_priority

    if not properties:
        return {"success": False, "message": "No properties specified to set"}

    try:
        response = unreal.send_command("set_rvt_properties", {
            "asset_path": asset_path,
            "properties": properties
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_rvt_properties error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_data_asset(asset_path: str) -> Dict[str, Any]:
    """Read a DataAsset (or any UObject asset) and return all UPROPERTY fields serialized to JSON.

    Args:
        asset_path: Full asset path, e.g. "/Game/Data/Weapons/DA_Katana" or "/Game/Data/Weapons/DA_Katana.DA_Katana"
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_data_asset", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_data_asset(asset_path: str, properties: Dict[str, Any], save: bool = False) -> Dict[str, Any]:
    """Update properties on a DataAsset (or any UObject asset). Only specified properties are modified.

    Args:
        asset_path: Full asset path, e.g. "/Game/Data/Weapons/DA_Katana"
        properties: Dict of property_name -> new_value. Supports nested structs, arrays, maps, enums, gameplay tags, etc.
        save: If True, save the asset to disk after updating. Default: mark dirty only.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path, "properties": properties}
        if save:
            params["save"] = True
        response = unreal.send_command("update_data_asset", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_data_asset(asset_path: str, class_name: str, properties: Dict[str, Any] = None, save: bool = False) -> Dict[str, Any]:
    """Create a new DataAsset (or any UObject asset) at the given path.

    Args:
        asset_path: Full asset path for the new asset, e.g. "/Game/Data/Weapons/DA_NewSword"
        class_name: Class to instantiate. Short name (e.g. "PRKWeaponData") or full path ("/Script/PRK.PRKWeaponData").
        properties: Optional dict of property_name -> value to set on the new asset. Same format as update_data_asset.
        save: If True, save the asset to disk after creation. Default: mark dirty only.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path, "class_name": class_name}
        if properties is not None:
            params["properties"] = properties
        if save:
            params["save"] = True
        response = unreal.send_command("create_data_asset", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def populate_athena_preset(
    asset_path: str,
    tasks: List[Dict[str, Any]],
    fallback_task: Dict[str, Any] = None,
    save: bool = False,
    generate_curves: bool = True
) -> Dict[str, Any]:
    """Populate an AthenaAI Agent Preset's Tasks array with inline task instances.

    Each task dict must have a '_class' field with the task class name
    (e.g. 'UPRKAthenaTask_HealSelf' or '/Script/PRK.UPRKAthenaTask_HealSelf').
    Constructor-set properties (TaskTag, Considerations) are auto-populated by
    the C++ constructor. Only pass property overrides you want to change from defaults.

    This command also:
    - Rebuilds ConsiderationTags from all tasks
    - Regenerates consideration curve table rows (if generate_curves=True)

    Args:
        asset_path: Full asset path to the preset, e.g. "/Game/PRK/AI/AthenaAI/AthenaPresets/AthenaAI_CivilianPreset"
        tasks: List of task definitions. Each is a dict with '_class' and optional property overrides
               (e.g. {"_class": "UPRKAthenaTask_HealSelf", "Priority": 60}).
        fallback_task: Optional single task definition for the FallbackTask property.
        save: If True, save preset and curve table to disk after populating.
        generate_curves: If True (default), regenerate consideration curves after populating tasks.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path, "tasks": tasks}
        if fallback_task is not None:
            params["fallback_task"] = fallback_task
        if save:
            params["save"] = True
        if not generate_curves:
            params["generate_curves"] = False
        response = unreal.send_command("populate_athena_preset", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"populate_athena_preset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_data_assets(directory: str = "/Game/", limit: int = 100, class_filter: str = None) -> Dict[str, Any]:
    """List DataAssets in a directory, optionally filtered by class.

    Args:
        directory: Content directory to search, e.g. "/Game/" or "/Game/Data/Weapons/"
        limit: Maximum number of results to return (default 100)
        class_filter: Optional class name or full path to filter by, e.g. "PRKWeaponData" or "/Script/PRK.PRKWeaponData"
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"directory": directory, "limit": limit}
        if class_filter is not None:
            params["class_filter"] = class_filter
        response = unreal.send_command("list_data_assets", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_data_assets error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_data_table(asset_path: str, limit: int = 200) -> Dict[str, Any]:
    """Read a DataTable and return its schema (field names + types) and all rows serialized to JSON.

    Args:
        asset_path: Full asset path to the DataTable, e.g. "/Game/Data/DT_ItemDatabase"
        limit: Maximum number of rows to return (default 200)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_data_table", {"asset_path": asset_path, "limit": limit})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_data_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_data_table_row(asset_path: str, row_name: str) -> Dict[str, Any]:
    """Read a single row from a DataTable by row name.

    Args:
        asset_path: Full asset path to the DataTable, e.g. "/Game/Data/DT_ItemDatabase"
        row_name: The row name (key) to look up
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_data_table_row", {"asset_path": asset_path, "row_name": row_name})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_data_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_curve_table(asset_path: str, curve_mode: str = "RichCurves", rows: Dict[str, Any] = None, save: bool = False) -> Dict[str, Any]:
    """Create a new CurveTable asset.

    Args:
        asset_path: Full asset path for the new CurveTable, e.g. "/Game/Data/CT_DamageScaling"
        curve_mode: "RichCurves" (default) or "SimpleCurves".
        rows: Optional initial rows. Dict of row_name -> {keys: [{time, value, ...}], default_value: float}.
              For RichCurves, each key supports: time, value, interp_mode, tangent_mode, arrive_tangent, leave_tangent.
              For SimpleCurves, each key supports: time, value. The row can also set 'interp_mode'.
        save: If True, save the asset to disk after creation. Default: mark dirty only.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path, "curve_mode": curve_mode}
        if rows is not None:
            params["rows"] = rows
        if save:
            params["save"] = True
        response = unreal.send_command("create_curve_table", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_curve_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_curve_table(asset_path: str, row_names: List[str] = None) -> Dict[str, Any]:
    """Read a CurveTable and return all curve rows with their keys.

    Args:
        asset_path: Full asset path to the CurveTable, e.g. "/Game/Data/CT_DamageScaling"
        row_names: Optional list of specific row names to read. If omitted, reads all rows.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path}
        if row_names is not None:
            params["row_names"] = row_names
        response = unreal.send_command("read_curve_table", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_curve_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_curve_table(asset_path: str, rows: Dict[str, Any] = None, remove_rows: List[str] = None, save: bool = False) -> Dict[str, Any]:
    """Update a CurveTable: add/modify curve rows or remove rows.

    Each row in 'rows' is a dict with optional 'keys' array and 'default_value'.
    For RichCurves, each key supports: time, value, interp_mode (Linear/Constant/Cubic/None),
    tangent_mode (Auto/User/Break/SmartAuto/None), arrive_tangent, leave_tangent.
    For SimpleCurves, each key supports: time, value. The row can also set 'interp_mode' (Linear/Constant).

    Args:
        asset_path: Full asset path to the CurveTable, e.g. "/Game/Data/CT_DamageScaling"
        rows: Dict of row_name -> {keys: [{time, value, ...}], default_value: float}. Missing rows are created.
        remove_rows: List of row names to remove from the table.
        save: If True, save the asset to disk after updating. Default: mark dirty only.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path}
        if rows is not None:
            params["rows"] = rows
        if remove_rows is not None:
            params["remove_rows"] = remove_rows
        if save:
            params["save"] = True
        response = unreal.send_command("update_curve_table", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_curve_table error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================
# Asset Import Tools
# ============================================================

@mcp.tool()
def import_fbx(
    source_file: str,
    destination_path: str = "/Game/Meshes",
    asset_name: str = "",
    combine_meshes: bool = True,
    compute_normals: bool = True,
    import_materials: bool = True,
    import_textures: bool = True,
    auto_generate_collision: bool = True,
    scale_factor: float = 1.0,
    build_nanite: bool = False
) -> Dict[str, Any]:
    """Import an FBX file as a static mesh with configurable settings.

    Handles common import issues automatically:
    - Missing smoothing groups (compute_normals=True computes normals from geometry)
    - Separate mesh parts (combine_meshes=True merges into single mesh)
    - Scale conversion (scale_factor adjusts if model uses meters vs cm)

    Args:
        source_file: Absolute path to the FBX file on disk
        destination_path: Content Browser path (e.g., "/Game/PRK/Items")
        asset_name: Override the imported asset name (empty = use filename)
        combine_meshes: Merge all sub-meshes into a single static mesh
        compute_normals: Compute normals from geometry (fixes faceted imports)
        import_materials: Import/create materials from the FBX file
        import_textures: Import textures referenced by materials
        auto_generate_collision: Generate simple collision if none in FBX
        scale_factor: Uniform scale multiplier (1.0 = no change, 100.0 = meters to cm)
        build_nanite: Enable Nanite on the imported mesh
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("import_fbx", {
            "source_file": source_file,
            "destination_path": destination_path,
            "asset_name": asset_name,
            "combine_meshes": combine_meshes,
            "compute_normals": compute_normals,
            "import_materials": import_materials,
            "import_textures": import_textures,
            "auto_generate_collision": auto_generate_collision,
            "scale_factor": scale_factor,
            "build_nanite": build_nanite
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"import_fbx error: {e}")
        return {"success": False, "message": str(e)}


# =============================================================================
# Widget Blueprint Tools
# =============================================================================

@mcp.tool()
def analyze_widget_blueprint(asset_path: str) -> Dict[str, Any]:
    """Analyze a Widget Blueprint's widget tree structure.

    Returns the full widget hierarchy including widget classes, names,
    visibility, slot info, and type-specific properties (text, brush, percent, etc.).

    Args:
        asset_path: Full asset path to the Widget Blueprint (e.g., "/Game/UI/WBP_HUD")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("analyze_widget_blueprint", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_widget_details(asset_path: str, widget_name: str) -> Dict[str, Any]:
    """Get detailed property dump of a specific widget in a Widget Blueprint.

    Returns all UPROPERTY values via reflection, including slot properties.

    Args:
        asset_path: Full asset path to the Widget Blueprint
        widget_name: Name of the widget to inspect (e.g., "TextBlock_0")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_widget_details", {
            "asset_path": asset_path,
            "widget_name": widget_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_widget_details error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_widget_blueprint(
    asset_path: str,
    root_widget_class: str = "CanvasPanel",
    parent_class: str = "UserWidget"
) -> Dict[str, Any]:
    """Create a new Widget Blueprint asset.

    Args:
        asset_path: Full asset path for the new widget (e.g., "/Game/UI/WBP_NewWidget")
        root_widget_class: Class for the root panel widget. Default "CanvasPanel".
                           Common values: CanvasPanel, VerticalBox, HorizontalBox, Overlay
        parent_class: Parent UserWidget class. Default "UserWidget".
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "root_widget_class": root_widget_class,
            "parent_class": parent_class
        }
        response = unreal.send_command("create_widget_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_widget_child(
    asset_path: str,
    widget_class: str,
    widget_name: str,
    parent_name: str = "",
    properties: Dict[str, Any] = None,
    slot_properties: Dict[str, Any] = None,
    index: int = -1
) -> Dict[str, Any]:
    """Add a widget to a Widget Blueprint's widget tree.

    If parent_name is empty, sets the widget as root (fails if root exists).
    After adding widgets, call compile_widget_blueprint to finalize changes.

    Args:
        asset_path: Full asset path to the Widget Blueprint
        widget_class: Widget class to create. Short name or full path. Common types:
                      TextBlock, Button, Image, ProgressBar, CanvasPanel,
                      HorizontalBox, VerticalBox, Overlay, Border, SizeBox,
                      ScrollBox, Spacer, CheckBox, Slider, EditableTextBox,
                      RichTextBlock, ComboBoxString, WidgetSwitcher, GridPanel, WrapBox
        widget_name: Name for the new widget (e.g., "HealthBar")
        parent_name: Name of parent panel widget. Empty string = set as root.
        properties: Optional dict of UPROPERTY values to set on the widget
                    (e.g., {"Text": "Hello", "ColorAndOpacity": {"R": 1, "G": 0, "B": 0, "A": 1}})
        slot_properties: Optional dict of properties to set on the widget's slot
        index: Child index for insertion order (-1 = append to end)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_class": widget_class,
            "widget_name": widget_name,
        }
        if parent_name:
            params["parent_name"] = parent_name
        if properties is not None:
            params["properties"] = properties
        if slot_properties is not None:
            params["slot_properties"] = slot_properties
        if index >= 0:
            params["index"] = index
        response = unreal.send_command("add_widget_child", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_widget_child error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_properties(
    asset_path: str,
    widget_name: str,
    properties: Dict[str, Any] = None,
    slot_properties: Dict[str, Any] = None
) -> Dict[str, Any]:
    """Set properties on a widget and/or its slot in a Widget Blueprint.

    Uses UPROPERTY reflection, so any exposed property can be set.
    Only properties present in the dict are modified; others are untouched.

    Args:
        asset_path: Full asset path to the Widget Blueprint
        widget_name: Name of the widget to modify
        properties: Dict of property_name -> value for the widget itself
        slot_properties: Dict of property_name -> value for the widget's slot
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_name": widget_name,
        }
        if properties is not None:
            params["properties"] = properties
        if slot_properties is not None:
            params["slot_properties"] = slot_properties
        response = unreal.send_command("set_widget_properties", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_properties error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_widget(asset_path: str, widget_name: str) -> Dict[str, Any]:
    """Remove a widget from a Widget Blueprint's widget tree.

    If the widget is the root, clears the root. Removes the widget and all
    its sub-widgets from the tree.

    Args:
        asset_path: Full asset path to the Widget Blueprint
        widget_name: Name of the widget to remove
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_widget", {
            "asset_path": asset_path,
            "widget_name": widget_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_widget error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def replace_widget_root(
    asset_path: str,
    new_root_class: str,
    new_root_name: str = "",
    migrate_children: bool = True
) -> Dict[str, Any]:
    """Replace the root widget of a Widget Blueprint.

    Optionally migrates children from the old root to the new root.

    Args:
        asset_path: Full asset path to the Widget Blueprint
        new_root_class: Class for the new root (must be a panel widget, e.g.,
                        CanvasPanel, VerticalBox, HorizontalBox, Overlay)
        new_root_name: Name for the new root. Empty = auto-generated from class name.
        migrate_children: If True, moves children from old root to new root.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "new_root_class": new_root_class,
            "migrate_children": migrate_children
        }
        if new_root_name:
            params["new_root_name"] = new_root_name
        response = unreal.send_command("replace_widget_root", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"replace_widget_root error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def compile_widget_blueprint(asset_path: str) -> Dict[str, Any]:
    """Compile a Widget Blueprint after making changes.

    Should be called after structural changes (add/remove widgets) or
    property changes that affect bindings.

    Args:
        asset_path: Full asset path to the Widget Blueprint
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("compile_widget_blueprint", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"compile_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


# =============================================================================
# Skeletal Mesh Tools
# =============================================================================

@mcp.tool()
def get_skeletal_mesh_info(
    asset_path: str,
    include_bones: bool = True
) -> Dict[str, Any]:
    """Read a Skeletal Mesh asset and return its sockets, bones, and metadata.

    Use this to inspect what sockets and bones exist on a character skeleton,
    diagnose weapon attachment issues, or verify socket names match weapon data assets.

    Args:
        asset_path: Full asset path to the SkeletalMesh
                    (e.g., "/Game/Characters/SK_Mannequin")
        include_bones: Include full bone hierarchy. Default True.
                       Set to False for large skeletons to reduce output size.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "include_bones": include_bones
        }
        response = unreal.send_command("get_skeletal_mesh_info", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_skeletal_mesh_info error: {e}")
        return {"success": False, "message": str(e)}


# =============================================================================
# Animation Blueprint Tools
# =============================================================================

@mcp.tool()
def create_anim_blueprint(
    name: str,
    skeleton_path: str,
    parent_class: str = "AnimInstance",
    path: str = "/Game/AnimBlueprints/",
    preview_mesh: str = ""
) -> Dict[str, Any]:
    """Create a new Animation Blueprint with a target skeleton.

    Args:
        name: Asset name for the new AnimBlueprint
        skeleton_path: Content path to the USkeleton asset (e.g., "/Game/Characters/Mannequin/Skeleton")
        parent_class: Parent class name (default: AnimInstance). Can be a custom UAnimInstance subclass.
        path: Content Browser folder to create in (default: /Game/AnimBlueprints/)
        preview_mesh: Optional path to a preview skeletal mesh
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "name": name,
            "skeleton_path": skeleton_path,
            "parent_class": parent_class,
            "path": path,
        }
        if preview_mesh:
            params["preview_mesh"] = preview_mesh
        response = unreal.send_command("create_anim_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_anim_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_anim_blueprint(
    name: str = "",
    asset_path: str = ""
) -> Dict[str, Any]:
    """Read and analyze an existing Animation Blueprint's structure.

    Returns skeleton, parent class, variables, state machines (with states and transitions).

    Args:
        name: Asset name to search for in common directories
        asset_path: Full content path to the AnimBlueprint asset
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {}
        if name:
            params["name"] = name
        if asset_path:
            params["asset_path"] = asset_path
        response = unreal.send_command("read_anim_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_anim_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_state_machine(
    blueprint_name: str,
    name: str = "Locomotion",
    pos_x: float = 200.0,
    pos_y: float = 0.0
) -> Dict[str, Any]:
    """Add a state machine node to an Animation Blueprint's AnimGraph.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        name: Name for the state machine (default: Locomotion)
        pos_x: X position in the AnimGraph
        pos_y: Y position in the AnimGraph
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_state_machine", {
            "blueprint_name": blueprint_name,
            "name": name,
            "pos_x": pos_x,
            "pos_y": pos_y,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_state_machine error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_anim_state(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str,
    pos_x: float = 300.0,
    pos_y: float = 0.0
) -> Dict[str, Any]:
    """Add a state to a state machine in an Animation Blueprint.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine to add the state to
        state_name: Name for the new state
        pos_x: X position in the state machine graph
        pos_y: Y position in the state machine graph
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_state", {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "pos_x": pos_x,
            "pos_y": pos_y,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_anim_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_anim_transition(
    blueprint_name: str,
    state_machine_name: str,
    from_state: str,
    to_state: str,
    crossfade_duration: float = 0.2,
    priority: int = 1
) -> Dict[str, Any]:
    """Add a transition between two states in a state machine.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine
        from_state: Source state name
        to_state: Target state name
        crossfade_duration: Blend duration in seconds (default: 0.2)
        priority: Transition priority (lower = higher priority, default: 1)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_transition", {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
            "from_state": from_state,
            "to_state": to_state,
            "crossfade_duration": crossfade_duration,
            "priority": priority,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_anim_transition error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_default_anim_state(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str
) -> Dict[str, Any]:
    """Set the default (entry) state for a state machine.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine
        state_name: Name of the state to set as default entry point
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_default_state", {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_default_anim_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_state_animation(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str,
    animation_path: str,
    loop: bool = True
) -> Dict[str, Any]:
    """Set the animation for a state by placing a Sequence Player node inside it.

    Creates an AnimGraphNode_SequencePlayer inside the state's graph and connects
    it to the state's output pose result node.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine
        state_name: Name of the state to set animation for
        animation_path: Content path to the animation sequence asset
        loop: Whether the animation should loop (default: True)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_state_animation", {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "animation_path": animation_path,
            "loop": loop,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_state_animation error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_transition_rule(
    blueprint_name: str,
    state_machine_name: str,
    from_state: str,
    to_state: str,
    rule_type: str = "automatic",
    automatic_rule_trigger_time: float = -1.0,
    trigger_time: float = 0.0,
    crossfade_duration: float = -1.0
) -> Dict[str, Any]:
    """Configure the transition rule for an existing transition.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine
        from_state: Source state name
        to_state: Target state name
        rule_type: 'automatic' (triggers based on sequence remaining time) or 'time_remaining' (manual threshold)
        automatic_rule_trigger_time: For 'automatic': < 0 means trigger crossfade_duration before end, >= 0 is explicit time before end
        trigger_time: For 'time_remaining': seconds before animation end to trigger
        crossfade_duration: Override crossfade duration (< 0 to keep existing)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
            "from_state": from_state,
            "to_state": to_state,
            "rule_type": rule_type,
        }
        if rule_type == "automatic":
            params["automatic_rule_trigger_time"] = automatic_rule_trigger_time
        elif rule_type == "time_remaining":
            params["trigger_time"] = trigger_time
        if crossfade_duration >= 0:
            params["crossfade_duration"] = crossfade_duration
        response = unreal.send_command("set_transition_rule", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_transition_rule error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_blend_space_player(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str,
    blend_space_path: str
) -> Dict[str, Any]:
    """Add a Blend Space Player node inside a state, replacing any existing pose connection.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine
        state_name: Name of the state to add the blend space to
        blend_space_path: Content path to the BlendSpace asset
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_blend_space_player", {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
            "state_name": state_name,
            "blend_space_path": blend_space_path,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_blend_space_player error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def connect_anim_nodes(
    blueprint_name: str,
    source_node_id: Union[str, int],
    target_node_id: Union[str, int],
    source_pin: str = "Pose",
    target_pin: str = "Result"
) -> Dict[str, Any]:
    """Connect two AnimGraph nodes by their pin names.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        source_node_id: Unique ID of the source node (returned by add_* commands). Accepts string or numeric ID.
        target_node_id: Unique ID of the target node. Accepts string or numeric ID.
        source_pin: Name of the output pin on the source node (default: Pose)
        target_pin: Name of the input pin on the target node (default: Result)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("connect_anim_nodes", {
            "blueprint_name": blueprint_name,
            "source_node_id": str(source_node_id),
            "target_node_id": str(target_node_id),
            "source_pin": source_pin,
            "target_pin": target_pin,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"connect_anim_nodes error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_state_machine_info(
    blueprint_name: str,
    state_machine_name: str
) -> Dict[str, Any]:
    """Get detailed information about a state machine including all states, transitions, and their properties.

    Args:
        blueprint_name: Name or path of the AnimBlueprint
        state_machine_name: Name of the state machine to inspect
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_state_machine_info", {
            "blueprint_name": blueprint_name,
            "state_machine_name": state_machine_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_state_machine_info error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_anim_node(
    asset_path: str,
    node_type: str,
    pos_x: float = 0.0,
    pos_y: float = 0.0,
    bound_bone: str = "",
    chain_end: str = "",
    is_chain: bool = True,
    gravity_scale: float = 1.0,
    linear_damping: float = 0.8,
    angular_damping: float = 0.8,
    use_attached_parent: bool = True,
    copy_curves: bool = True
) -> Dict[str, Any]:
    """Add an AnimGraph node (CopyPoseFromMesh or AnimDynamics) to an Animation Blueprint.

    Returns the node_id which can be used with connect_anim_nodes to wire nodes together.

    Args:
        asset_path: Full content path to the AnimBlueprint (e.g., "/Game/PRK/Items/Meshes/Shackles/ABP_Shackles")
        node_type: Type of node to add. Supported: "CopyPoseFromMesh", "AnimDynamics"
        pos_x: X position in the AnimGraph
        pos_y: Y position in the AnimGraph
        bound_bone: (AnimDynamics) Name of the bone to attach physics body to (e.g., "chain_01")
        chain_end: (AnimDynamics) End bone of chain (e.g., "chain_07"). Enables chain mode.
        is_chain: (AnimDynamics) Whether to simulate as a chain between bound_bone and chain_end
        gravity_scale: (AnimDynamics) Gravity multiplier (default 1.0)
        linear_damping: (AnimDynamics) Linear damping override (default 0.8)
        angular_damping: (AnimDynamics) Angular damping override (default 0.8)
        use_attached_parent: (CopyPoseFromMesh) Use attached parent as source mesh (default true)
        copy_curves: (CopyPoseFromMesh) Copy animation curves from source (default true)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "node_type": node_type,
            "pos_x": pos_x,
            "pos_y": pos_y,
        }
        if node_type.lower() == "copyposefrommesh":
            params["use_attached_parent"] = use_attached_parent
            params["copy_curves"] = copy_curves
        elif node_type.lower() == "animdynamics":
            if bound_bone:
                params["bound_bone"] = bound_bone
            if chain_end:
                params["chain_end"] = chain_end
            params["is_chain"] = is_chain
            params["gravity_scale"] = gravity_scale
            params["linear_damping"] = linear_damping
            params["angular_damping"] = angular_damping
        response = unreal.send_command("add_anim_node", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_anim_node error: {e}")
        return {"success": False, "message": str(e)}


# Run the server
if __name__ == "__main__":
    logger.info("Starting Advanced MCP server with stdio transport")
    mcp.run(transport='stdio')
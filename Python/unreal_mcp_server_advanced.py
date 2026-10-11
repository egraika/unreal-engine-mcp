"""
Unreal Engine Advanced MCP Server

A streamlined MCP server focused on advanced composition tools for Unreal Engine.
Contains only the advanced tools from the expanded MCP tool system to keep tool count manageable.
"""

import logging
import os
import socket
import json
import math
import select
import struct
import time
import threading
from contextlib import asynccontextmanager
from typing import AsyncIterator, Dict, Any, Optional, List
import base64
from mcp.server.fastmcp import FastMCP, Image

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
from helpers import prk_python_guard
from helpers import prk_texture_preview
from helpers import prk_backup
from helpers import prk_zone_catalog
from helpers import prk_zone_runner
from helpers import prk_zone_analysis
from helpers import prk_terrain_author_host
from pathlib import Path


# Logging — quiet by default. Set UNREALMCP_DEBUG=1 to enable DEBUG-level trace.
_log_level = logging.DEBUG if os.environ.get("UNREALMCP_DEBUG") == "1" else logging.WARNING
logging.basicConfig(
    level=_log_level,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('unreal_mcp_advanced.log'),
    ]
)
logger = logging.getLogger("UnrealMCP_Advanced")

# Configuration
UNREAL_HOST = os.environ.get("UNREAL_MCP_HOST", "127.0.0.1")
# Set UNREAL_MCP_PORT to point this server at a second editor started with
# -MCPPort=<port>.  Defaults to the single-editor port, so existing setups are
# unaffected.
UNREAL_PORT = int(os.environ.get("UNREAL_MCP_PORT", "55557"))


class CommandExecutionUncertain(Exception):
    """Payload was put on the wire but no response came back (transport broke
    or the editor crashed mid-command). The command may have partially executed,
    so a non-idempotent (mutating) command must NOT be blindly re-sent — blind
    replay is what turns a single editor crash into a reconnect crash-storm.
    Surfaced to the caller as an error; never retried."""
    pass


# Read-only command prefixes whose replay has no side effects, so they remain
# safe to retry even after the payload was already sent. Anything not matching
# is treated as mutating and gets at-most-once delivery.
_IDEMPOTENT_PREFIXES = (
    "get_", "read_", "list_", "find_", "analyze_", "describe_",
    "search_", "validate_", "capture_", "tail_", "is_", "has_",
)


def _is_idempotent_command(command: str) -> bool:
    """True if re-sending `command` after a mid-flight failure is harmless."""
    return command.startswith(_IDEMPOTENT_PREFIXES)


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
    # Mask bakes and alpha-cutout sampling legitimately run 20-40 minutes on
    # the editor game thread (2026-07-31: DarkKnightRobe class). A short recv
    # timeout turns those into fire-and-forget calls whose results are lost;
    # the editor keeps executing either way, so waiting is strictly better.
    LARGE_OP_RECV_TIMEOUT = 3600  # seconds for large operations
    BUFFER_SIZE = 65536  # match UE's 256KB kernel recv buffer in larger chunks
    
    # Commands that need longer timeouts
    LARGE_OPERATION_COMMANDS = {
        "get_available_materials",
        "create_town",
        "create_castle_fortress",
        "construct_mansion",
        "create_suspension_bridge",
        "create_aqueduct",
        "create_maze",
        "run_python",
        "run_python_file",
        "fixup_redirectors",
        "find_asset_referencers",
        "replace_asset_references",
        # Block the game thread until done (FinishAllCompilation / checked save);
        # a 30 s timeout reported these as "editor may have crashed" mid-compile.
        "wait_for_shaders",
        "wait_for_asset_compile",
        "save_asset_checked"
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
            # Narrow to OSError: a bare ``except:`` also swallows KeyboardInterrupt /
            # SystemExit, which can wedge shutdown. Socket teardown only raises OSError
            # (already-closed / half-open); anything else should propagate.
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None
        self.connected = False
    
    def _peer_closed_unsafe(self) -> bool:
        """True if the idle socket's peer already closed it (FIN/RST unread).

        Request/response protocol: between commands nothing should be readable,
        so readability means the editor went away (restart/crash). Checked
        BEFORE sendall, so reconnecting is safe even for non-idempotent commands.
        """
        if getattr(self, "_recv_leftover", b""):
            return False
        try:
            readable, _, errored = select.select([self.socket], [], [self.socket], 0)
            if errored:
                return True
            if not readable:
                return False
            self.socket.setblocking(False)
            try:
                return self.socket.recv(1, socket.MSG_PEEK) == b""
            finally:
                self.socket.settimeout(self.CONNECT_TIMEOUT)
        except BlockingIOError:
            return False
        except (OSError, ValueError):
            return True
    
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
        Receive newline-terminated response from Unreal.

        The C++ server frames every response with a trailing '\\n'. We read
        until we see it, then return the payload bytes (no trailing newline).
        Any bytes after the newline are stashed on self._recv_leftover for
        the next call — supports persistent connections with pipelined reads.

        Raises:
            TimeoutError / ConnectionError on transport failure.
        """
        timeout = self._get_timeout_for_command(command_type)
        self.socket.settimeout(timeout)

        buffer = getattr(self, "_recv_leftover", b"")
        self._recv_leftover = b""

        # Fast path: leftover already contains a full message.
        nl = buffer.find(b"\n")
        if nl != -1:
            self._recv_leftover = buffer[nl + 1:]
            return buffer[:nl]

        # Rely on the kernel's settimeout — no parallel time.time() bookkeeping.
        # bytearray + extend avoids quadratic copy on `bytes += chunk`.
        chunks = bytearray(buffer)
        while True:
            try:
                chunk = self.socket.recv(self.BUFFER_SIZE)
            except socket.timeout:
                raise TimeoutError(f"Recv timeout for {command_type} ({len(chunks)} bytes so far)")

            if not chunk:
                if chunks:
                    raise ConnectionError(f"Connection closed mid-response ({len(chunks)} bytes)")
                raise ConnectionError("Connection closed before receiving any data")

            chunks.extend(chunk)
            nl = chunks.find(b"\n")
            if nl != -1:
                self._recv_leftover = bytes(chunks[nl + 1:])
                return bytes(chunks[:nl])

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
            except CommandExecutionUncertain as e:
                # Payload already delivered for a mutating command. Replaying it
                # would repeat its side effects (the reconnect crash-storm).
                # Surface once, never retry.
                logger.error(str(e))
                self.disconnect()
                return {"status": "error", "error": str(e)}
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
        # Persistent-connection model: reconnect only when the socket is
        # not connected or a prior recv/send raised. RLock keeps the
        # send-receive cycle atomic against parallel tool calls.
        with self._lock:
            # Probe the idle socket: an editor restart leaves a stale socket
            # whose FIN/RST went unread. Catch it here, before anything is sent,
            # so the reconnect cannot replay a command.
            if not self.connected or self.socket is None or self._peer_closed_unsafe():
                if not self.connect():
                    raise ConnectionError(f"Failed to connect to Unreal Engine: {self._last_error}")
                self._recv_leftover = b""

            try:
                wire_params = {k: v for k, v in (params or {}).items() if k != "idempotent"}
                command_obj = {"type": command, "params": wire_params}
                # Newline-framed. Matches C++ server's response framing.
                payload = json.dumps(command_obj).encode('utf-8') + b"\n"

                logger.debug(f"Sending command: {command}")

                self.socket.settimeout(10)
                self.socket.sendall(payload)

                response_data = self._receive_response(command)

                try:
                    response = json.loads(response_data.decode('utf-8'))
                except json.JSONDecodeError as e:
                    logger.error(f"JSON decode error: {e}")
                    raise ValueError(f"Invalid JSON response: {e}")

                if response.get("status") == "error":
                    logger.warning(f"Unreal returned error: {response.get('error')}")
                elif response.get("success") is False:
                    response = {"status": "error", "error": response.get("error") or response.get("message", "Unknown error")}

                return response

            except (ConnectionError, TimeoutError, socket.error, OSError) as e:
                # Transport broke AFTER the payload was put on the wire. The
                # editor may have already executed (or crashed on) this command.
                # Drop the socket so the next call reconnects.
                self._close_socket_unsafe()
                if isinstance(e, (TimeoutError, socket.timeout)):
                    # A timeout is not a lost payload: the editor has the command
                    # and is still running it on the game thread. Re-sending, even
                    # a read, queues a duplicate behind it (4x the load on a
                    # hitching editor). Surface once, never replay.
                    raise CommandExecutionUncertain(
                        f"'{command}' got no response within "
                        f"{self._get_timeout_for_command(command)}s ({e}). The editor "
                        f"received it and is likely still executing it (busy game "
                        f"thread); not re-sending. Check the editor before re-issuing."
                    ) from e
                caller_idempotent = isinstance(params, dict) and params.get("idempotent", False)
                if _is_idempotent_command(command) or caller_idempotent:
                    # Read-only: replay is harmless → let the retry loop run.
                    raise
                # Mutating: re-sending replays the side effect — and if the
                # command crashed the editor, replays the crash on every
                # reconnect. Refuse to retry; surface once.
                raise CommandExecutionUncertain(
                    f"'{command}' was sent but no response was received ({e}). "
                    f"Not retrying a non-idempotent command — the editor may "
                    f"have crashed; check its state before re-issuing."
                ) from e

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
    """Delete an actor by name. Errant Landscape brushes are refused (use errant_stamp_delete)."""
    refused = _errant_brush_guard([], [name], "delete_actor")
    if refused:
        return refused
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

    # GUARD: the C++ handler unconditionally prepends "/Game/Blueprints/" to
    # `name` and calls CreatePackage(). A path-qualified name (one containing a
    # slash) yields a double-slash package path ("/Game/Blueprints//Game/...")
    # which trips a FATAL CreatePackage assert and takes the editor down. Refuse
    # it here with a pointer to the safe path instead of crashing the editor.
    if "/" in name or "\\" in name:
        return {
            "success": False,
            "message": (
                f"create_blueprint expects a BARE asset name, not a path (got "
                f"'{name}'). A path-qualified name crashes the editor "
                f"(CreatePackage double-slash assert). Pass a bare name then "
                f"rename_asset, or create at an exact path via run_python + "
                f"AssetTools.create_asset / BlueprintFactory."
            ),
        }

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
def add_blueprint_component(
    blueprint_name: str,
    component_type: str,
    component_name: str,
    parent_component: str = "",
    location: List[float] = [],
    rotation: List[float] = [],
    scale: List[float] = []
) -> Dict[str, Any]:
    """Add a component to a Blueprint, reliably resolved and correctly attached.

    The robust replacement for add_component_to_blueprint, which fails to resolve normal
    short type names and always adds at the SCS root (orphaned components whose relative
    transform looks inert). This one:
      - resolves component_type via TryFindTypeSlow with U-prefix / "Component"-suffix /
        "/Script/..." fallbacks, so "StaticMeshComponent", "StaticMesh", "UStaticMeshComponent",
        and "/Script/Engine.StaticMeshComponent" all work;
      - attaches under parent_component when given — an existing SCS node OR a native component
        (e.g. the native scene root or a native mesh). Fails cleanly if the parent isn't found,
        instead of silently orphaning at the root;
      - returns the post-compile state (resolved_class, parent, transform) to verify it stuck.

    Args:
        blueprint_name: Name or full asset path of the Blueprint.
        component_type: Component class — short name, U-prefixed, "/Script/..." path, or a name
            needing the "Component" suffix.
        component_name: Variable name for the new component (must be unique on the blueprint).
        parent_component: Optional. Name of an existing component (SCS or native) to attach under.
            Ignored for non-scene components. Empty = add at the root.
        location, rotation, scale: Optional relative transform as [x,y,z] / [pitch,yaw,roll] / [x,y,z].
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "blueprint_name": blueprint_name,
            "component_type": component_type,
            "component_name": component_name,
        }
        if parent_component:
            params["parent_component"] = parent_component
        if location:
            params["location"] = location
        if rotation:
            params["rotation"] = rotation
        if scale:
            params["scale"] = scale
        response = unreal.send_command("add_blueprint_component", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_blueprint_component error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def reparent_blueprint_component(
    blueprint_name: str,
    component_name: str,
    new_parent_component: str = "",
    location: List[float] = [],
    rotation: List[float] = [],
    scale: List[float] = []
) -> Dict[str, Any]:
    """Move an EXISTING Blueprint component under a new parent — the non-destructive
    complement to add_blueprint_component.

    SCS node-graph editing is editor C++ and not Python-reachable, so this is the only
    reliable way to restructure a blueprint's component hierarchy (e.g. reparent a door
    mesh under a hinge-pivot scene component) without deleting and recreating it — which
    would drop the mesh assignment and any other overrides.

    Args:
        blueprint_name: Name or full asset path of the Blueprint.
        component_name: The existing scene component to move (must be a scene component).
        new_parent_component: The component to attach under — an existing SCS node OR a
            native component (e.g. DefaultSceneRoot). Empty = reparent to the SCS root.
            Fails cleanly if not found, or if it would create a cycle.
        location, rotation, scale: Optional RELATIVE transform to set AFTER reparenting
            ([x,y,z] / [pitch,yaw,roll] / [x,y,z]) — its meaning changes under the new
            parent, so usually you set it here in the same call.

    Returns the post-compile parent + relative transform to verify it stuck.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
        }
        if new_parent_component:
            params["new_parent_component"] = new_parent_component
        if location:
            params["location"] = location
        if rotation:
            params["rotation"] = rotation
        if scale:
            params["scale"] = scale
        response = unreal.send_command("reparent_blueprint_component", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reparent_blueprint_component error: {e}")
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
def save_blueprint(blueprint_name: str, only_if_dirty: bool = True) -> Dict[str, Any]:
    """Save a Blueprint's package to disk.

    Args:
        blueprint_name: Name or full asset path of the Blueprint.
        only_if_dirty: If True, skip saving when the package is clean.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("save_blueprint", {
            "blueprint_name": blueprint_name,
            "only_if_dirty": only_if_dirty,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"save_blueprint error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def list_blueprints(
    path: str = "/Game",
    recursive: bool = True,
    name_contains: str = ""
) -> Dict[str, Any]:
    """Enumerate Blueprint assets in the project.

    Args:
        path: Package path to search (default: "/Game").
        recursive: Include subfolders (default: True).
        name_contains: Optional substring filter on asset name (case-sensitive).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"path": path, "recursive": recursive}
        if name_contains:
            params["name_contains"] = name_contains
        response = unreal.send_command("list_blueprints", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_blueprints error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_blueprint_components(blueprint_name: str) -> Dict[str, Any]:
    """List components on a Blueprint's SimpleConstructionScript.

    Returns each component's name, class, parent (for attachment hierarchy),
    and relative transform for scene components.

    Args:
        blueprint_name: Name or full asset path of the Blueprint.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_blueprint_components", {
            "blueprint_name": blueprint_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_blueprint_components error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def list_blueprint_components(blueprint_name: str) -> Dict[str, Any]:
    """List ALL components on a Blueprint — native (C++ constructor) AND SCS, including
    SCS inherited from parent Blueprints.

    The reliable replacement for get_blueprint_components (which returns SCS-only, no native
    components, no mesh) and for the fragile SubobjectDataSubsystem enumeration. Each entry has:
        name, class, source ("native" | "scs" | "inherited_scs"), parent (attach parent name),
        mesh (static/skeletal mesh asset path, when the component has one),
        transform {location, rotation, scale} for scene components.
    Top-level result also returns count, native_count, scs_count.

    Args:
        blueprint_name: Name or full asset path of the Blueprint.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_blueprint_components", {
            "blueprint_name": blueprint_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_blueprint_components error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def remove_component_from_blueprint(
    blueprint_name: str,
    component_name: str
) -> Dict[str, Any]:
    """Remove a component from a Blueprint's SimpleConstructionScript.

    Uses USimpleConstructionScript::RemoveNodeAndPromoteChildren so any
    attached children are reparented under the removed node's parent.

    Args:
        blueprint_name: Blueprint that owns the component.
        component_name: SCS node name to remove.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_component_from_blueprint", {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_component_from_blueprint error: {e}")
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
    material_path: str
) -> Dict[str, Any]:
    """
    Get all parameters from a Material Instance or base Material.

    Returns scalar, vector, texture, and static switch parameters with their
    current values and override status. For base Materials (not instances),
    returns the parameter names available for overriding with their defaults.

    Args:
        material_path: Full path to the Material or MaterialInstance asset
            (e.g., "/Game/Materials/MI_Blood_Drip")

    Returns:
        Dictionary with parameter arrays: scalar_parameters, vector_parameters,
        texture_parameters, static_switch_parameters, etc.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        logger.info(f"Getting material instance parameters: {material_path}")
        response = unreal.send_command("get_material_instance_parameters", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_instance_parameters error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def set_material_instance_parameters(
    material_path: str,
    scalar_parameters: List[Dict[str, Any]] = None,
    vector_parameters: List[Dict[str, Any]] = None,
    texture_parameters: List[Dict[str, Any]] = None,
    static_switch_parameters: List[Dict[str, Any]] = None
) -> Dict[str, Any]:
    """
    Set parameters on a MaterialInstanceConstant asset.

    Only works with MaterialInstanceConstant assets (not base Materials or MIDs).
    Each parameter entry needs a "name" and "value" field.

    Args:
        material_path: Full path to the MaterialInstanceConstant asset
        scalar_parameters: List of {"name": str, "value": float} dicts
        vector_parameters: List of {"name": str, "value": [R, G, B, A]} dicts
        texture_parameters: List of {"name": str, "value": "/Game/path"} dicts
        static_switch_parameters: List of {"name": str, "value": bool} dicts

    Returns:
        Dictionary with counts of parameters set and any errors
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        if scalar_parameters:
            params["scalar_parameters"] = scalar_parameters
        if vector_parameters:
            params["vector_parameters"] = vector_parameters
        if texture_parameters:
            params["texture_parameters"] = texture_parameters
        if static_switch_parameters:
            params["static_switch_parameters"] = static_switch_parameters

        logger.info(f"Setting material instance parameters: {material_path}")
        response = unreal.send_command("set_material_instance_parameters", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_material_instance_parameters error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_material_expressions(
    material_path: str
) -> Dict[str, Any]:
    """
    Get all expressions from a UMaterial asset.

    Returns every expression node with its type, inputs, outputs, connections,
    editor position, and specialized properties (parameter names, values, etc.).

    Args:
        material_path: Full path to the Material asset
            (e.g., "/Game/Materials/M_Landscape")

    Returns:
        Dictionary with expression_count and expressions array
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        logger.info(f"Getting material expressions: {material_path}")
        response = unreal.send_command("get_material_expressions", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_expressions error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_material_connections(
    material_path: str
) -> Dict[str, Any]:
    """
    Get material output pin connections for a UMaterial asset.

    Returns which expressions connect to each material output property
    (BaseColor, Metallic, Normal, Roughness, etc.).

    Args:
        material_path: Full path to the Material asset
            (e.g., "/Game/Materials/M_Landscape")

    Returns:
        Dictionary with connections array and connected_outputs (filtered)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        logger.info(f"Getting material connections: {material_path}")
        response = unreal.send_command("get_material_connections", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_connections error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_material_function_info(
    function_path: str
) -> Dict[str, Any]:
    """
    Get detailed info about a UMaterialFunction asset.

    Returns function inputs, outputs, internal expressions, library categories,
    and description. Works with any MaterialFunction asset.

    Args:
        function_path: Full path to the MaterialFunction asset
            (e.g., "/Game/MaterialFunctions/MF_LayerBlend_WeightBlend")

    Returns:
        Dictionary with inputs, outputs, expressions, and metadata
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"function_path": function_path}
        logger.info(f"Getting material function info: {function_path}")
        response = unreal.send_command("get_material_function_info", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_function_info error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def get_material_stats(
    material_path: str
) -> Dict[str, Any]:
    """
    Get stats and metadata for a UMaterial asset.

    Returns domain, blend mode, shading models, two-sided flag, expression counts,
    and parameter counts (scalar, vector, texture, static switch).

    Args:
        material_path: Full path to the Material asset
            (e.g., "/Game/Materials/M_Landscape")

    Returns:
        Dictionary with material properties and expression/parameter counts
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        params = {"material_path": material_path}
        logger.info(f"Getting material stats: {material_path}")
        response = unreal.send_command("get_material_stats", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_material_stats error: {e}")
        return {"success": False, "message": str(e)}

# ============================================================================
# Material graph EDITING (write surface)
# ============================================================================
# Every write tool below targets EITHER a UMaterial (material_path) or a
# UMaterialFunction (function_path) — pass exactly one. Returns a dict with
# success=True or success=False + error_code + message. Common error_codes:
#   AssetNotLoadable, AssetClassMismatch, MissingOrAmbiguousTarget,
#   ExpressionIndexOutOfRange, ClassNotResolved, InputNameNotFound,
#   OutputNameNotFound, PinTypeMismatch, ConnectionRejected,
#   ConnectionVerificationFailed, DeleteVerificationFailed,
#   ExpressionNotDeletable, UnknownMaterialProperty, PropertyNotFound,
#   PropertySetFailed, SaveFailed, BatchOperationFailed.
# All write operations run inside FScopedTransaction (Ctrl+Z undoable).
# ============================================================================

def _material_target(material_path: Optional[str], function_path: Optional[str]) -> Dict[str, str]:
    """Build the target-field params dict. Exactly one of the two must be set."""
    if bool(material_path) == bool(function_path):
        raise ValueError("Provide exactly one of material_path or function_path")
    return {"material_path": material_path} if material_path else {"function_path": function_path}

def _send_material_command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command(command, params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"{command} error: {e}")
        return {"success": False, "message": str(e)}

@mcp.tool()
def material_create_expression(
    expression_class: str,
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
    x: int = 0,
    y: int = 0,
) -> Dict[str, Any]:
    """
    Create a new UMaterialExpression node in a Material or MaterialFunction.

    Wraps UMaterialEditingLibrary::CreateMaterialExpression(InFunction)
    inside FScopedTransaction with post-condition verification that the new
    expression actually landed in the target's expressions array. For
    UMaterialExpressionFunctionInput/Output, the FGuid Id is auto-repaired
    if the engine left it invalid (per PRK feedback_function_input_output_id).

    Args:
        expression_class: Bare name ("Constant"), prefixed
            ("MaterialExpressionConstant"), or full path
            ("/Script/Engine.MaterialExpressionConstant").
        material_path: Path to UMaterial. Mutually exclusive with function_path.
        function_path: Path to UMaterialFunction.
        x, y: Editor node position.

    Returns:
        Dict with expression_index (int), expression_class, x, y on success.
    """
    params = _material_target(material_path, function_path)
    params.update({"expression_class": expression_class, "x": x, "y": y})
    return _send_material_command("material_create_expression", params)

@mcp.tool()
def material_delete_expression(
    expression_index: int,
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Delete a material expression by index. Auto-disconnects referencing inputs
    and reports their expression indices so the caller knows what was unwired.

    Returns:
        Dict with deleted_index and auto_disconnected_from_expression_indices.
    """
    params = _material_target(material_path, function_path)
    params["expression_index"] = expression_index
    return _send_material_command("material_delete_expression", params)

@mcp.tool()
def material_connect_expressions(
    from_index: int,
    to_index: int,
    from_output_name: str = "",
    to_input_name: str = "",
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Connect From.output → To.input with pin-type validation and post-condition
    verification. Empty output/input names default to the first pin.

    Validation:
        - Both indices must resolve.
        - Output/input names must exist on their expressions.
        - EMaterialValueType bitmasks must overlap (PinTypeMismatch otherwise).
    Verification:
        - After the engine's ConnectMaterialExpressions call returns true,
          the input pin is read back to confirm it actually references the
          source expression. The library famously returns true on some
          partial failures, so the verify step is load-bearing.
    """
    params = _material_target(material_path, function_path)
    params.update({
        "from_index": from_index,
        "to_index": to_index,
        "from_output_name": from_output_name,
        "to_input_name": to_input_name,
    })
    return _send_material_command("material_connect_expressions", params)

@mcp.tool()
def material_connect_property(
    from_index: int,
    property: str,
    material_path: str,
    from_output_name: str = "",
) -> Dict[str, Any]:
    """
    Connect an expression output to a material property pin (BaseColor,
    Roughness, Normal, etc.). Only valid for UMaterial — not for
    UMaterialFunction (which uses FunctionOutput nodes instead).

    Valid property names: BaseColor, Metallic, Specular, Roughness,
    Anisotropy, EmissiveColor, Opacity, OpacityMask, Normal, Tangent,
    WorldPositionOffset, Displacement, SubsurfaceColor, ClearCoat,
    ClearCoatRoughness, AmbientOcclusion, Refraction, PixelDepthOffset,
    ShadingModel, MaterialAttributes, FrontMaterial.
    """
    return _send_material_command("material_connect_property", {
        "material_path": material_path,
        "from_index": from_index,
        "from_output_name": from_output_name,
        "property": property,
    })

@mcp.tool()
def material_disconnect_input(
    expression_index: int,
    input_name: str = "",
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Disconnect a named input on a material expression. Empty input_name targets
    the first input. Verifies the input is nulled after disconnect.
    """
    params = _material_target(material_path, function_path)
    params.update({"expression_index": expression_index, "input_name": input_name})
    return _send_material_command("material_disconnect_input", params)

@mcp.tool()
def material_set_expression_position(
    expression_index: int,
    x: int,
    y: int,
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
) -> Dict[str, Any]:
    """Move an expression node to (x, y) in the editor graph."""
    params = _material_target(material_path, function_path)
    params.update({"expression_index": expression_index, "x": x, "y": y})
    return _send_material_command("material_set_expression_position", params)

@mcp.tool()
def material_set_expression_property(
    expression_index: int,
    property_name: str,
    value: Any,
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Set any UPROPERTY on a material expression. Delegates to the reflection
    setter, so every property exposed via UPROPERTY is reachable without
    per-class wrappers.

    Examples:
        # Set a Constant expression's value:
        material_set_expression_property(7, "R", 0.5, material_path="...")

        # Set a ScalarParameter's default + group:
        material_set_expression_property(3, "DefaultValue", 0.5, material_path="...")
        material_set_expression_property(3, "ParameterName", "MyParam", material_path="...")

        # Set a TextureSample's texture:
        material_set_expression_property(9, "Texture", "/Game/Textures/T_Foo", material_path="...")

        # Set Constant3Vector's color:
        material_set_expression_property(2, "Constant",
                                          {"R": 0.8, "G": 0.2, "B": 0.1, "A": 1.0},
                                          material_path="...")
    """
    params = _material_target(material_path, function_path)
    params.update({
        "expression_index": expression_index,
        "property_name": property_name,
        "value": value,
    })
    return _send_material_command("material_set_expression_property", params)

@mcp.tool()
def material_recompile(material_path: str) -> Dict[str, Any]:
    """
    Trigger a recompile of a UMaterial. Required after graph edits before the
    changes are visible in materials using the asset.
    """
    return _send_material_command("material_recompile", {"material_path": material_path})

@mcp.tool()
def material_update_function(function_path: str) -> Dict[str, Any]:
    """
    Update a UMaterialFunction after edits. Recompiles every Material that
    uses this function.
    """
    return _send_material_command("material_update_function", {"function_path": function_path})

@mcp.tool()
def material_save(
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
) -> Dict[str, Any]:
    """Save the Material or MaterialFunction asset to disk."""
    params = _material_target(material_path, function_path)
    return _send_material_command("material_save", params)

@mcp.tool()
def material_batch_edit(
    operations: List[Dict[str, Any]],
    material_path: Optional[str] = None,
    function_path: Optional[str] = None,
    recompile: bool = False,
    save: bool = False,
) -> Dict[str, Any]:
    """
    Run multiple material edits inside a SINGLE FScopedTransaction. If ANY
    operation fails, the entire transaction is canceled and the asset reverts
    to its pre-batch state. This is the right tool for multi-step edits where
    a partial result would be wrong.

    Each operations entry is a dict with an "op" field plus the op's params
    (everything you would pass to the individual tool, minus material_path
    and function_path which are inherited from this call).

    Supported op values:
        create_expression, delete_expression, connect_expressions,
        connect_property, disconnect_input, set_expression_position,
        set_expression_property

    Example:
        material_batch_edit(
            material_path="/Game/Materials/M_Foo",
            operations=[
                {"op": "create_expression", "expression_class": "Constant3Vector", "x": -300, "y": 0},
                {"op": "set_expression_property", "expression_index": 7,
                 "property_name": "Constant", "value": {"R":0.5,"G":0.5,"B":0.5,"A":1}},
                {"op": "connect_property", "from_index": 7, "property": "BaseColor"},
            ],
            recompile=True,
            save=True,
        )

    Failure response includes failed_op_index, failed_op, and op_response so
    you can localize the error without re-running the whole batch in trace mode.
    """
    params = _material_target(material_path, function_path)
    params.update({
        "operations": operations,
        "recompile": recompile,
        "save": save,
    })
    return _send_material_command("material_batch_edit", params)

# ============================================================================
# Generalized texture import — role-aware presets
# ============================================================================

@mcp.tool()
def import_texture(
    source_file: str,
    destination_path: str,
    asset_name: str,
    role: str,
    replace_existing: bool = True,
    delete_source_after_import: bool = False,
    srgb: Optional[bool] = None,
    compression: Optional[str] = None,
    lod_group: Optional[str] = None,
    never_stream: Optional[bool] = None,
    no_mips: Optional[bool] = None,
) -> Dict[str, Any]:
    """
    Import a texture with role-aware presets that lock in the correct
    combination of compression / sRGB / LOD group / mip settings.

    Role drives presets:
        Color        : sRGB=true,  TC_Default,   World group, mips on
        Normal       : sRGB=false, TC_Normalmap, WorldNormalMap group, mips on
        ORD / Mask   : sRGB=false, TC_Masks,     World group, mips on
        Displacement : sRGB=false, TC_Grayscale, World group, mips on
        BlueNoise    : sRGB=false, TC_Masks,     World group, NO mips
        UI           : sRGB=true,  TC_EditorIcon, UI group, NO mips, NeverStream

    Per-role overrides: pass srgb/compression/lod_group/never_stream/no_mips
    explicitly to override the role default for that field.

    Why this exists: per PRK feedback_ord_texture_import, Megascans ORDs ship
    with default Color settings, and importing as Color silently breaks
    SAMPLERTYPE_Masks shader compiles downstream. Encoding the role mapping
    centrally makes the failure mode "unknown role" up front instead of
    "shader compile broke 200 materials later."

    Returns:
        Dict with asset_path, role, size_x, size_y, srgb, compression_settings,
        lod_group. Includes "warning" if any setting failed to hold after
        PostEditChange (factory override).
    """
    params = {
        "source_file": source_file,
        "destination_path": destination_path,
        "asset_name": asset_name,
        "role": role,
        "replace_existing": replace_existing,
        "delete_source_after_import": delete_source_after_import,
    }
    if srgb         is not None: params["srgb"] = srgb
    if compression  is not None: params["compression"] = compression
    if lod_group    is not None: params["lod_group"] = lod_group
    if never_stream is not None: params["never_stream"] = never_stream
    if no_mips      is not None: params["no_mips"] = no_mips
    return _send_material_command("import_texture", params)

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
def delete_variable(
    blueprint_name: str,
    variable_name: str
) -> Dict[str, Any]:
    """Remove a member variable from a Blueprint.

    Uses FBlueprintEditorUtils::RemoveMemberVariable, which patches every
    VariableGet / VariableSet reference and recompiles the Blueprint.

    Args:
        blueprint_name: Blueprint that owns the variable.
        variable_name: Variable to remove.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_variable", {
            "blueprint_name": blueprint_name,
            "variable_name": variable_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_variable error: {e}")
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
    or a specific function graph. Works on regular Blueprints and on
    AnimBlueprint EventGraph / function graphs (use delete_anim_node
    for nodes that live inside the AnimGraph or state-inner BoundGraphs).

    Args:
        blueprint_name: Path or name of the Blueprint to modify. A full
            path (e.g. /Game/Characters/.../ABP_Foo) is preferred.
            Bare names are resolved against /Game/Blueprints/,
            /Game/AnimBlueprints/, /Game/Characters/, and /Game/.
        node_id: ID of the node to delete. Accepts:
                 - integer UniqueID returned by read_anim_blueprint /
                   analyze_blueprint_graph (e.g. "76183"),
                 - NodeGuid hex string,
                 - node GetName().
        function_name: Name of function graph (optional, defaults to
            UbergraphPages[0] which is the EventGraph for both
            Blueprints and AnimBlueprints).

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
        - AnimGraph nodes (UAnimGraphNode_Base + subclasses): any field on the
          inner FAnimNode_* struct, looked up case-insensitively. Supported types:
            * Scalars: int, float, double, bool, byte, FName, FString.
              "LODThreshold" (int), "MinRootMotionSpeedThreshold" (float),
              "BDisableIfMissingRootMotion" (bool), "PlayRate" (float),
              "BLoop" (bool), etc. Note: read_anim_blueprint dumps these
              with a lowercased first letter (e.g. "lODThreshold") — the
              setter accepts either form.
            * Object/asset references (FObjectProperty): pass an asset path
              string. The path is resolved via StaticLoadObject and assigned
              to the property. Empty string clears the reference.
              Example: property_name="MirrorDataTable",
                       property_value="/Game/.../MDT_PRKSkeleton.MDT_PRKSkeleton"
          Pass `function_name="AnimGraph"` (or the inner state graph name) to target
          AnimGraph nodes outside the EventGraph.
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


# =============================================================================
# Skeletal Mesh & Socket Tools
# =============================================================================

@mcp.tool()
def get_skeletal_mesh_info(
    asset_path: str,
    include_bones: bool = True
) -> Dict[str, Any]:
    """Read a Skeletal Mesh asset and return its sockets, bones, and metadata.

    Args:
        asset_path: Full asset path to the SkeletalMesh (e.g., "/Game/Characters/SK_Mannequin")
        include_bones: Include full bone hierarchy. Default True.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_skeletal_mesh_info", {
            "asset_path": asset_path,
            "include_bones": include_bones
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_skeletal_mesh_info error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_socket_to_skeleton(
    asset_path: str,
    socket_name: str,
    bone_name: str,
    location: Dict[str, float] = None,
    rotation: Dict[str, float] = None,
    scale: Dict[str, float] = None
) -> Dict[str, Any]:
    """Add a new socket to a USkeleton asset.

    The asset_path can point to either a USkeleton or a USkeletalMesh (will use its skeleton).

    Args:
        asset_path: Path to skeleton or skeletal mesh asset
        socket_name: Name for the new socket (e.g., "sheath_back")
        bone_name: Bone to attach the socket to (e.g., "spine_05")
        location: Optional relative location {"x": 0, "y": 0, "z": 0}
        rotation: Optional relative rotation {"pitch": 0, "yaw": 0, "roll": 0}
        scale: Optional relative scale {"x": 1, "y": 1, "z": 1}
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "socket_name": socket_name,
            "bone_name": bone_name
        }
        if location is not None:
            params["location"] = location
        if rotation is not None:
            params["rotation"] = rotation
        if scale is not None:
            params["scale"] = scale
        response = unreal.send_command("add_socket_to_skeleton", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_socket_to_skeleton error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def modify_socket(
    asset_path: str,
    socket_name: str,
    bone_name: str = "",
    location: Dict[str, float] = None,
    rotation: Dict[str, float] = None,
    scale: Dict[str, float] = None
) -> Dict[str, Any]:
    """Modify an existing socket's transform or parent bone on a skeleton.

    Only provided fields are updated; others remain unchanged.

    Args:
        asset_path: Path to skeleton or skeletal mesh asset
        socket_name: Name of the socket to modify
        bone_name: Optional new parent bone name
        location: Optional new relative location {"x": 0, "y": 0, "z": 0}
        rotation: Optional new relative rotation {"pitch": 0, "yaw": 0, "roll": 0}
        scale: Optional new relative scale {"x": 1, "y": 1, "z": 1}
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "socket_name": socket_name
        }
        if bone_name:
            params["bone_name"] = bone_name
        if location is not None:
            params["location"] = location
        if rotation is not None:
            params["rotation"] = rotation
        if scale is not None:
            params["scale"] = scale
        response = unreal.send_command("modify_socket", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"modify_socket error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_socket(asset_path: str, socket_name: str) -> Dict[str, Any]:
    """Remove a socket from a skeleton.

    Args:
        asset_path: Path to skeleton or skeletal mesh asset
        socket_name: Name of the socket to remove
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_socket", {
            "asset_path": asset_path,
            "socket_name": socket_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_socket error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def preview_mesh_on_socket(
    actor_name: str,
    socket_name: str,
    mesh_path: str,
    location_offset: Dict[str, float] = None,
    rotation_offset: Dict[str, float] = None,
    scale: Dict[str, float] = None
) -> Dict[str, Any]:
    """Spawn a temporary static mesh attached to a socket on an actor in the level.

    Works with actors whose root carrying the socket is either a
    USkeletalMeshComponent OR a UStaticMeshComponent. Use clear_socket_preview
    to remove previews.

    Args:
        actor_name: Name or label of the actor in the level
        socket_name: Socket to attach the preview mesh to
        mesh_path: Asset path to the static mesh to preview
        location_offset: Optional offset from socket {"x": 0, "y": 0, "z": 0}
        rotation_offset: Optional rotation offset {"pitch": 0, "yaw": 0, "roll": 0}
        scale: Optional scale {"x": 1, "y": 1, "z": 1}
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "actor_name": actor_name,
            "socket_name": socket_name,
            "mesh_path": mesh_path
        }
        if location_offset is not None:
            params["location_offset"] = location_offset
        if rotation_offset is not None:
            params["rotation_offset"] = rotation_offset
        if scale is not None:
            params["scale"] = scale
        response = unreal.send_command("preview_mesh_on_socket", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"preview_mesh_on_socket error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def clear_socket_preview(actor_name: str = "") -> Dict[str, Any]:
    """Remove preview meshes spawned by preview_mesh_on_socket.

    Args:
        actor_name: Clear previews for this actor only. Empty = clear all previews.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {}
        if actor_name:
            params["actor_name"] = actor_name
        response = unreal.send_command("clear_socket_preview", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"clear_socket_preview error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def capture_socket_preview(
    skeleton_path: str,
    socket_name: str,
    mesh_path: str,
    resolution: int = 512
) -> Dict[str, Any]:
    """Capture a screenshot of a mesh attached to a socket on a skeleton's preview mesh.

    Spawns the skeleton's preview mannequin, attaches the given mesh to the socket,
    renders a screenshot from a camera looking at the socket area, and returns the
    file path to the saved PNG image.

    Args:
        skeleton_path: Path to skeleton or skeletal mesh asset
        socket_name: Socket to attach the preview mesh to
        mesh_path: Asset path to the static/skeletal mesh to preview
        resolution: Screenshot resolution in pixels (default 512)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("capture_socket_preview", {
            "skeleton_path": skeleton_path,
            "socket_name": socket_name,
            "mesh_path": mesh_path,
            "resolution": resolution
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"capture_socket_preview error: {e}")
        return {"success": False, "message": str(e)}


# =============================================================================
# Static Mesh & Socket Tools
# =============================================================================

@mcp.tool()
def get_static_mesh_info(
    asset_path: str,
    include_materials: bool = True
) -> Dict[str, Any]:
    """Read a Static Mesh asset and return its sockets, LODs, materials, and bounds.

    Args:
        asset_path: Full asset path to the StaticMesh (e.g., "/Game/Meshes/SM_Chair")
        include_materials: Include material slot array. Default True.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_static_mesh_info", {
            "asset_path": asset_path,
            "include_materials": include_materials
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_static_mesh_info error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_mesh_footprint(
    asset_path: str,
    z_min: float = None,
    z_max: float = None,
    band_above_floor: float = None
) -> Dict[str, Any]:
    """Measure a Static Mesh's XY footprint within an optional local-Z band.

    Reads LOD0 vertices in C++ (no GeometryScript dependency). Use it to get a building's
    TRUE wall/floor footprint without the roof overhang inflating the bounding box — e.g. to
    center/size a foundation under the walls.

    Args:
        asset_path: Full asset path to the StaticMesh (e.g. "/Game/.../SM_House").
        z_min, z_max: Explicit local-Z band to measure between. If omitted, measures the whole mesh.
        band_above_floor: Convenience — measure from the mesh's lowest Z up to (lowest Z + this).
            e.g. 150 for a wall-base band. Ignored when z_min/z_max are given.

    Returns:
        full_bounds (min/max/center) + footprint {z_min, z_max, vertex_count, x_min, x_max,
        y_min, y_max, center_x, center_y, size_x, size_y}.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path}
        if z_min is not None:
            params["z_min"] = z_min
        if z_max is not None:
            params["z_max"] = z_max
        if band_above_floor is not None:
            params["band_above_floor"] = band_above_floor
        response = unreal.send_command("get_mesh_footprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_mesh_footprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_mesh_vertex_colors(
    asset_path: str,
    z_min: float = None,
    z_max: float = None,
    band_above_floor: float = None
) -> Dict[str, Any]:
    """Read per-channel vertex-color stats for a Static Mesh (optionally within a Z band).

    CRASH-FREE replacement for has_vertex_colors() (which hard-crashes the editor). Reads the
    LOD0 render ColorVertexBuffer in C++ — exactly what a vertex-color-reading material sees at
    render time (e.g. building-hider floor/quadrant tags). Use it to check whether a mesh carries
    the expected vertex tags before/after a bake.

    Args:
        asset_path: Full asset path to the StaticMesh.
        z_min, z_max: Explicit local-Z band to sample. If omitted, samples the whole mesh.
        band_above_floor: Convenience — sample from lowest Z up to (lowest Z + this). Ignored if
            z_min/z_max given.

    Returns: has_vertex_colors, total_vertices, sampled_vertices, and per-channel r/g/b/a
    {min,max,mean} (0-1, raw FColor/255, no sRGB). has_vertex_colors=false → flat white mesh.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path}
        if z_min is not None:
            params["z_min"] = z_min
        if z_max is not None:
            params["z_max"] = z_max
        if band_above_floor is not None:
            params["band_above_floor"] = band_above_floor
        response = unreal.send_command("get_mesh_vertex_colors", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_mesh_vertex_colors error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_mesh_geometry_in_box(
    source_asset: str,
    box_min: List[float],
    box_max: List[float],
    dest_asset: str = "",
    keep: str = "outside",
    recompute_normals: bool = True,
    save: bool = False
) -> Dict[str, Any]:
    """Delete a static mesh's triangles inside (or outside) a local-space box — for ruined/torn variants.

    Reads the mesh into a DynamicMesh in C++ (CopyMeshFromStaticMesh — the reader that is NOT
    reachable from run_python), removes triangles by centroid vs the box, and writes the result
    back via CopyMeshToStaticMesh, which PRESERVES the asset's materials and build settings
    (unlike create_new_static_mesh_asset_from_mesh, which wrecks materials and can fatally assert
    on missing UV0). The kept triangles carry their original UVs, so no UV crash.

    Args:
        source_asset: StaticMesh to read.
        box_min, box_max: Local-space AABB corners [x,y,z] (mesh-local cm, same frame as
            get_mesh_footprint).
        dest_asset: If given (and != source), duplicate source there and edit the copy; else edit
            source in place.
        keep: "outside" (default) deletes triangles INSIDE the box (cut a hole/breach);
            "inside" deletes triangles OUTSIDE the box (keep only that region).
        recompute_normals: Recompute normals after the cut (fixes shading on the new boundary).
        save: Save the asset after writing. Default False — prefer save_asset_checked afterward
            (the mesh may be read-only / under P4).

    Returns: source_asset, dest_asset, in_place, triangles_before/deleted/after, uv0_present.
    Aborts (error) if the edit would delete every triangle.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "source_asset": source_asset,
            "box_min": box_min,
            "box_max": box_max,
            "keep": keep,
            "recompute_normals": recompute_normals,
            "save": save,
        }
        if dest_asset:
            params["dest_asset"] = dest_asset
        response = unreal.send_command("delete_mesh_geometry_in_box", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_mesh_geometry_in_box error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_mesh_collision_info(asset_path: str) -> Dict[str, Any]:
    """Audit a Static Mesh's SIMPLE collision and navmesh-carving status.

    The diagnostic for "this baked/merged building shell won't carve navmesh" — Recast carves
    nav from SIMPLE collision only, so a shell with zero simple hulls + ComplexAsSimple blocks
    pawn traces but floods nav through its walls. Pairs with get_mesh_footprint.

    Returns: convex_count, box_count, sphere_count, capsule_count, simple_primitive_count,
    has_simple_collision, collision_trace_flag (UseDefault | SimpleAndComplex | SimpleAsComplex
    | ComplexAsSimple), nav_collision_present, carves_navmesh.

    Args:
        asset_path: Full asset path to the StaticMesh.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_mesh_collision_info", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_mesh_collision_info error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def generate_convex_collision(
    asset_path: str,
    hull_count: int = 144,
    max_hull_verts: int = 32,
    precision: int = 1000000
) -> Dict[str, Any]:
    """Run V-HACD convex decomposition and write it as the mesh's SIMPLE collision.

    The fix for baked/merged building shells that ship with zero simple hulls (so they don't
    carve navmesh). Returns the FINAL hull count read back from the body setup — verify it is
    > 0 and near hull_count. The asset is marked dirty; SAVE it afterward (save_asset / p4 add)
    to persist. Mesh must have CPU geometry (Nanite-only meshes have none).

    Args:
        asset_path: Full asset path to the StaticMesh.
        hull_count: Max number of convex hulls to generate. Default 144.
        max_hull_verts: Max vertices per hull. Default 32.
        precision: V-HACD voxel resolution / precision. Default 1000000.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("generate_convex_collision", {
            "asset_path": asset_path,
            "hull_count": hull_count,
            "max_hull_verts": max_hull_verts,
            "precision": precision,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"generate_convex_collision error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_socket_to_static_mesh(
    asset_path: str,
    socket_name: str,
    location: Dict[str, float] = None,
    rotation: Dict[str, float] = None,
    scale: Dict[str, float] = None,
    tag: str = ""
) -> Dict[str, Any]:
    """Add a new socket to a UStaticMesh asset.

    Static mesh sockets have no parent bone (static meshes are rigid). Transform
    is relative to the mesh origin.

    Args:
        asset_path: Path to the static mesh asset
        socket_name: Name for the new socket (e.g., "MuzzleFlash", "AttachPoint_01")
        location: Optional relative location {"x": 0, "y": 0, "z": 0}
        rotation: Optional relative rotation {"pitch": 0, "yaw": 0, "roll": 0}
        scale: Optional relative scale {"x": 1, "y": 1, "z": 1}
        tag: Optional free-form tag string stored on the socket
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "socket_name": socket_name
        }
        if location is not None:
            params["location"] = location
        if rotation is not None:
            params["rotation"] = rotation
        if scale is not None:
            params["scale"] = scale
        if tag:
            params["tag"] = tag
        response = unreal.send_command("add_socket_to_static_mesh", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_socket_to_static_mesh error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def modify_static_mesh_socket(
    asset_path: str,
    socket_name: str,
    location: Dict[str, float] = None,
    rotation: Dict[str, float] = None,
    scale: Dict[str, float] = None,
    tag: str = None
) -> Dict[str, Any]:
    """Modify an existing static-mesh socket. Only provided fields are updated.

    Args:
        asset_path: Path to the static mesh asset
        socket_name: Name of the socket to modify
        location: Optional new relative location {"x": 0, "y": 0, "z": 0}
        rotation: Optional new relative rotation {"pitch": 0, "yaw": 0, "roll": 0}
        scale: Optional new relative scale {"x": 1, "y": 1, "z": 1}
        tag: Optional new tag string (pass empty string to clear)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "socket_name": socket_name
        }
        if location is not None:
            params["location"] = location
        if rotation is not None:
            params["rotation"] = rotation
        if scale is not None:
            params["scale"] = scale
        if tag is not None:
            params["tag"] = tag
        response = unreal.send_command("modify_static_mesh_socket", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"modify_static_mesh_socket error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_static_mesh_socket(asset_path: str, socket_name: str) -> Dict[str, Any]:
    """Remove a socket from a static mesh.

    Args:
        asset_path: Path to the static mesh asset
        socket_name: Name of the socket to remove
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_static_mesh_socket", {
            "asset_path": asset_path,
            "socket_name": socket_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_static_mesh_socket error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def capture_static_mesh_socket_preview(
    static_mesh_path: str,
    socket_name: str,
    mesh_path: str,
    resolution: int = 512
) -> Dict[str, Any]:
    """Capture a screenshot of a mesh attached to a socket on a static mesh.

    Spawns the base static mesh, attaches the preview mesh (static OR skeletal)
    to the named socket, and renders a PNG. Mirror of capture_socket_preview for
    skeletal meshes.

    Args:
        static_mesh_path: Path to the base UStaticMesh carrying the socket
        socket_name: Socket to attach the preview mesh to
        mesh_path: Asset path to the preview mesh (static or skeletal)
        resolution: Screenshot resolution in pixels (default 512)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("capture_static_mesh_socket_preview", {
            "static_mesh_path": static_mesh_path,
            "socket_name": socket_name,
            "mesh_path": mesh_path,
            "resolution": resolution
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"capture_static_mesh_socket_preview error: {e}")
        return {"success": False, "message": str(e)}


# =============================================================================
# Widget Blueprint Preview Tools
# =============================================================================

def _widget_capture_to_image(response: Dict[str, Any]):
    """Convert a capture_widget_* JSON response into an inline FastMCP Image.

    Falls back to returning the raw response (with diagnostics) when the
    response is missing the base64 payload — typically the failure case.
    The PNG file on disk is preserved either way for debugging.
    """
    if not response:
        return {"success": False, "message": "No response from Unreal"}
    if not response.get("success"):
        return response
    b64 = response.get("image_base64")
    if not b64:
        return response
    try:
        png_bytes = base64.b64decode(b64)
    except Exception as decode_err:
        logger.error(f"widget capture base64 decode failed: {decode_err}")
        return response
    return Image(data=png_bytes, format="png")


@mcp.tool()
def capture_widget_preview(
    widget_path: str,
    resolution: int = 1024
) -> Any:
    """Render a Widget Blueprint to a PNG using offscreen FWidgetRenderer.

    Constructs a transient UUserWidget instance from the WidgetBlueprint's
    GeneratedClass at editor-time defaults, rasterizes its Slate tree into a
    UTextureRenderTarget2D (no world spawn, no PIE), and returns the PNG
    inline so the model sees the image directly without a follow-up read.

    The PNG is also written to ``Saved/MCP_Previews/widget_preview_*.png`` for
    debugging. Use this for layout/style/hierarchy verification — bound text
    and BP-graph-driven visibility will show defaults only. For data-bound
    captures use ``capture_widget_state``.

    Args:
        widget_path: Asset path to the WidgetBlueprint, e.g. ``/Game/UI/WB_Foo``
        resolution: Longest-edge pixel size; aspect ratio comes from desired size. Default 1024.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("capture_widget_preview", {
            "widget_path": widget_path,
            "resolution": resolution
        })
        return _widget_capture_to_image(response)
    except Exception as e:
        logger.error(f"capture_widget_preview error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def capture_widget_state(
    widget_path: str,
    mock_data: Dict[str, Any],
    resolution: int = 1024
) -> Any:
    """Render a Widget Blueprint with mock data applied to its instance properties.

    Same offscreen FWidgetRenderer pipeline as ``capture_widget_preview``, but
    applies ``mock_data`` to the constructed UUserWidget via reflection
    (FJsonObjectConverter::JsonObjectToUStruct) before rasterizing. Top-level
    keys must match UPROPERTY field names on the UserWidget subclass; unknown
    keys are silently skipped.

    Use this to verify how a widget renders with realistic state — e.g. a
    populated inventory grid, a non-empty health bar, a specific stance label.
    Returns the PNG inline so the model sees the image directly.

    Args:
        widget_path: Asset path to the WidgetBlueprint, e.g. ``/Game/UI/WB_Foo``
        mock_data: JSON property bag, top-level keys = UPROPERTY names on the widget class.
        resolution: Longest-edge pixel size; aspect ratio comes from desired size. Default 1024.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("capture_widget_state", {
            "widget_path": widget_path,
            "resolution": resolution,
            "mock_data": mock_data or {}
        })
        return _widget_capture_to_image(response)
    except Exception as e:
        logger.error(f"capture_widget_state error: {e}")
        return {"success": False, "message": str(e)}


# =============================================================================
# Animation Montage & Sequence Tools
# =============================================================================

@mcp.tool()
def analyze_anim_montage(
    asset_path: str,
    curve_sample_count: int = 16,
    warning_filter: Optional[List[str]] = None
) -> Dict[str, Any]:
    """Analyze a UAnimMontage asset. Returns round-trippable reflection data
    plus derived analytics (warnings, notify distribution, root motion
    summary, curve samples, effective play length, per-slot coverage).

    Read `warnings` first — it surfaces issues that otherwise require
    deriving them from the raw fields (overlaps, gaps, out-of-bounds notifies,
    broken section links, etc.).

    Reflected fields:
        asset_name, play_length, effective_play_length
        blend_in_time, blend_out_time, blend_in_mode, blend_out_mode,
        blend_out_trigger_time, enable_auto_blend_out,
        blend_profile_in, blend_profile_out
        root_motion_root_lock, enable_root_motion_translation, enable_root_motion_rotation
        sync_group, sync_slot_index
        rate_scale, loop
        blend_mode_in, blend_mode_out ("Standard"|"Inertialization")
        time_stretch_curve_name, time_stretch_curve: { SamplingRate, CurveValueMinPrecision, ... }
        preview_base_pose (editor-only)
        sections: [{ index, name, start_time, next_section, link_method,
                     metadata: [{ index, class, class_path, properties }] }]
        slot_tracks: [{ slot_index, slot_name, coverage_fraction,
                        segments: [{ segment_index, start_pos, anim_start_time,
                                     anim_end_time, anim_play_rate, looping_count,
                                     segment_end_pos, anim_reference }] }]
        slot_names: [str]
        notify_tracks: [{ index, name, color: { r, g, b, a } }]
        notifies: [{ index, notify_name, trigger_time, duration, end_time, is_state,
                     notify_class, notify_class_path, track_index, track_name,
                     tick_type ("Queued"|"BranchingPoint"),
                     trigger_time_offset, end_trigger_time_offset,
                     trigger_weight_threshold, notify_trigger_chance,
                     trigger_on_dedicated_server, trigger_on_follower,
                     properties: { ...reflected UPROPERTYs of the notify instance } }]

    Derived analytics:
        warnings: [{ code, severity, message, ...context }]
        notify_summary: { total, branching_points, queued, state_count, instant_count,
                          by_track: { name: count }, by_class: { class_path: count } }
        root_motion_summary: { slots: [{ slot_index, slot_name,
                                          segments: [{ segment_index, translation, rotation_euler,
                                                       distance_xy, distance_z }],
                                          slot_total: { ... } }],
                               montage_total_from_slot0: { ... } }
        curve_samples: [{ name, samples: [{ time, value, label? }] }]

    Warning codes:
        segment_overlap, segment_gap, notify_outside_segments, notify_past_montage_end,
        notify_track_index_invalid, notify_same_time_same_track, section_next_broken,
        section_time_out_of_range, orphan_notify_track, empty_slot, sync_slot_invalid,
        time_stretch_curve_missing

    Args:
        asset_path: Full asset path to the AnimMontage
        curve_sample_count: Uniform samples per float curve (default 16). Samples at
            section starts and notify times are always included. Set 0 to emit only
            labeled times.
        warning_filter: List of warning codes to suppress from output.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "curve_sample_count": curve_sample_count,
        }
        if warning_filter:
            params["warning_filter"] = warning_filter
        response = unreal.send_command("analyze_anim_montage", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_anim_montage error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def analyze_anim_sequence(
    asset_path: str,
    curve_sample_count: int = 16,
    warning_filter: Optional[List[str]] = None
) -> Dict[str, Any]:
    """Analyze a UAnimSequence. Returns play length, frame rate, bone tracks,
    curves, notifies, plus derived analytics (warnings, curve samples, root
    motion summary).

    Sequence-level flags (for diagnosing skating / drift without opening the editor):
        enable_root_motion: bool — master gate for root-motion extraction.
        root_motion_root_lock: "RefPose" | "AnimFirstFrame" | "Zero".
        force_root_lock: bool — lock root even when root motion is disabled.
        use_normalized_root_motion_scale: bool.
        interpolation: "Linear" | "Step".
        additive_anim_type: "None" | "LocalSpaceBase" | "RotationOffsetMeshSpace".
        ref_pose_type: "None" | "RefPose" | "AnimScaled" | "AnimFrame" | "LocalAnimFrame".
        ref_pose_seq: asset path (or "None") — additive base animation.
        ref_frame_index: int — frame into ref_pose_seq for AnimFrame / LocalAnimFrame.
        bone_compression_settings / curve_compression_settings /
        variable_frame_stripping_settings: asset paths (or "None").

    Derived analytics:
        warnings: [{ code, severity, message, ...context }]
            Codes: notify_track_index_invalid, notify_past_montage_end,
                   notify_same_time_same_track
        curve_samples: [{ name, samples: [{ time, value, label? }] }]
        root_motion_summary: { translation, rotation_euler, distance_xy, distance_z }

    Args:
        asset_path: Full asset path to the AnimSequence
        curve_sample_count: Uniform samples per float curve (default 16). Samples at
            notify trigger times are always included.
        warning_filter: List of warning codes to suppress.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "curve_sample_count": curve_sample_count,
        }
        if warning_filter:
            params["warning_filter"] = warning_filter
        response = unreal.send_command("analyze_anim_sequence", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_anim_sequence error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_animation_assets(
    directory: str = "/Game/",
    limit: int = 100
) -> Dict[str, Any]:
    """List UAnimMontage, UAnimSequence, and UBlendSpace assets in a directory.

    Args:
        directory: Content Browser folder path to search (default: "/Game/")
        limit: Maximum number of results to return (default: 100)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_animation_assets", {
            "directory": directory,
            "limit": limit
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_animation_assets error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_anim_montage(
    name: str,
    skeleton_path: str,
    source_animation: str = "",
    slot_name: str = "DefaultSlot",
    package_path: str = "/Game/Animations/",
    blend_in_time: float = 0.25,
    blend_out_time: float = 0.25,
    blend_in_mode: str = "",
    blend_out_mode: str = "",
    blend_out_trigger_time: Optional[float] = None,
    enable_auto_blend_out: Optional[bool] = None,
    blend_profile_in_path: str = "",
    blend_profile_out_path: str = "",
    root_motion_root_lock: str = "",
    enable_root_motion_translation: Optional[bool] = None,
    enable_root_motion_rotation: Optional[bool] = None,
    sync_group: str = "",
    sync_slot_index: Optional[int] = None,
    rate_scale: Optional[float] = None,
    loop: Optional[bool] = None,
    blend_mode_in: str = "",
    blend_mode_out: str = "",
    time_stretch_curve_name: str = "",
    time_stretch_sampling_rate: Optional[float] = None,
    time_stretch_min_precision: Optional[float] = None,
    preview_base_pose_path: str = "",
    notify_tracks: Optional[List[Dict[str, Any]]] = None,
    sections: Optional[List[Dict[str, Any]]] = None,
    segments: Optional[List[Dict[str, Any]]] = None,
    notifies: Optional[List[Dict[str, Any]]] = None
) -> Dict[str, Any]:
    """Create a new UAnimMontage asset atomically with optional full configuration.

    Basic:
        name, skeleton_path, source_animation, slot_name, package_path,
        blend_in_time, blend_out_time.

    Blend envelope:
        blend_in_mode / blend_out_mode: "Linear" | "Cubic" | "HermiteCubic" |
            "Sinusoidal" | "QuadraticInOut" | "CubicInOut" | "QuarticInOut" |
            "QuinticInOut" | "CircularIn" | "CircularOut" | "CircularInOut" |
            "ExpIn" | "ExpOut" | "ExpInOut" | "Custom"
        blend_out_trigger_time, enable_auto_blend_out,
        blend_profile_in_path, blend_profile_out_path.

    Root motion:
        root_motion_root_lock: "RefPose" | "AnimFirstFrame" | "Zero"
        enable_root_motion_translation, enable_root_motion_rotation.

    Sync group:
        sync_group (FName string), sync_slot_index.

    Initial content (each array's element schema matches the corresponding
    individual `add_*` tool):
        notify_tracks: [{ name, color?: { r,g,b,a } }]
        sections:      [{ name, start_time?, next_section_name?, link_method? }]
        segments:      [{ animation_path, slot_name?|slot_index?, start_pos?,
                          anim_start_time?, anim_end_time?, play_rate?, looping_count? }]
        notifies:      [{ notify_name, trigger_time, duration?,
                          notify_class_path?|notify_class?, track_name?|track_index?,
                          tick_type?, end_trigger_time_offset?,
                          trigger_weight_threshold?, notify_trigger_chance?,
                          trigger_on_dedicated_server?, trigger_on_follower?,
                          properties? }]
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "name": name,
            "skeleton_path": skeleton_path,
            "slot_name": slot_name,
            "package_path": package_path,
            "blend_in_time": blend_in_time,
            "blend_out_time": blend_out_time,
        }
        if source_animation:
            params["source_animation"] = source_animation
        if blend_in_mode:
            params["blend_in_mode"] = blend_in_mode
        if blend_out_mode:
            params["blend_out_mode"] = blend_out_mode
        if blend_out_trigger_time is not None:
            params["blend_out_trigger_time"] = blend_out_trigger_time
        if enable_auto_blend_out is not None:
            params["enable_auto_blend_out"] = enable_auto_blend_out
        if blend_profile_in_path:
            params["blend_profile_in_path"] = blend_profile_in_path
        if blend_profile_out_path:
            params["blend_profile_out_path"] = blend_profile_out_path
        if root_motion_root_lock:
            params["root_motion_root_lock"] = root_motion_root_lock
        if enable_root_motion_translation is not None:
            params["enable_root_motion_translation"] = enable_root_motion_translation
        if enable_root_motion_rotation is not None:
            params["enable_root_motion_rotation"] = enable_root_motion_rotation
        if sync_group:
            params["sync_group"] = sync_group
        if sync_slot_index is not None:
            params["sync_slot_index"] = sync_slot_index
        if rate_scale is not None:
            params["rate_scale"] = rate_scale
        if loop is not None:
            params["loop"] = loop
        if blend_mode_in:
            params["blend_mode_in"] = blend_mode_in
        if blend_mode_out:
            params["blend_mode_out"] = blend_mode_out
        if time_stretch_curve_name:
            params["time_stretch_curve_name"] = time_stretch_curve_name
        stretch_obj: Dict[str, Any] = {}
        if time_stretch_sampling_rate is not None:
            stretch_obj["SamplingRate"] = time_stretch_sampling_rate
        if time_stretch_min_precision is not None:
            stretch_obj["CurveValueMinPrecision"] = time_stretch_min_precision
        if stretch_obj:
            params["time_stretch_curve"] = stretch_obj
        if preview_base_pose_path:
            params["preview_base_pose_path"] = preview_base_pose_path
        if notify_tracks:
            params["notify_tracks"] = notify_tracks
        if sections:
            params["sections"] = sections
        if segments:
            params["segments"] = segments
        if notifies:
            params["notifies"] = notifies
        response = unreal.send_command("create_anim_montage", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_anim_montage error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_montage_section(
    asset_path: str,
    section_name: str,
    start_time: float,
    next_section_name: str = "",
    link_method: str = ""
) -> Dict[str, Any]:
    """Add a composite section to an AnimMontage.

    Args:
        asset_path: Path to the AnimMontage asset
        section_name: Name for the new section (e.g., "Loop", "Recovery")
        start_time: Start time in seconds within the montage
        next_section_name: Optional name of the section to play after this one
        link_method: "Absolute" (default) | "Relative" | "Proportional"
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "section_name": section_name,
            "start_time": start_time
        }
        if next_section_name:
            params["next_section_name"] = next_section_name
        if link_method:
            params["link_method"] = link_method
        response = unreal.send_command("add_montage_section", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_montage_section error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_montage_section(
    asset_path: str,
    section_name: str
) -> Dict[str, Any]:
    """Remove a composite section from an AnimMontage by name.

    Args:
        asset_path: Path to the AnimMontage asset
        section_name: Name of the section to remove
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_montage_section", {
            "asset_path": asset_path,
            "section_name": section_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_montage_section error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_section_link(
    asset_path: str,
    section_name: str,
    next_section_name: str
) -> Dict[str, Any]:
    """Link two montage sections so one flows into the other.

    Args:
        asset_path: Path to the AnimMontage asset
        section_name: Source section name
        next_section_name: Section to play after source completes (use "None" to unlink)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_montage_section_link", {
            "asset_path": asset_path,
            "section_name": section_name,
            "next_section_name": next_section_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_section_link error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_montage_notify(
    asset_path: str,
    notify_name: str,
    trigger_time: float,
    duration: float = 0.0,
    notify_class: str = "",
    notify_class_path: str = "",
    track_index: int = 0,
    track_name: str = "",
    tick_type: str = "",
    end_trigger_time_offset: Optional[float] = None,
    trigger_weight_threshold: Optional[float] = None,
    notify_trigger_chance: Optional[float] = None,
    trigger_on_dedicated_server: Optional[bool] = None,
    trigger_on_follower: Optional[bool] = None,
    properties: Optional[Dict[str, Any]] = None
) -> Dict[str, Any]:
    """Add an animation notify event to a montage.

    If duration > 0, creates a state notify (has both start and end events).
    If notify_class_path or notify_class is provided, creates a typed notify instance.
    notify_class_path (full path, e.g. "/Script/Engine.AnimNotifyState_MotionWarping")
    is preferred; notify_class (short name) is a fallback with ambiguity risk.

    Args:
        asset_path: Path to the AnimMontage asset
        notify_name: Name for the notify (e.g., "HitWindow", "PlaySound")
        trigger_time: Time in seconds when the notify fires
        duration: Duration in seconds for state notifies (0 = instant notify)
        notify_class: Short class name fallback (collision-prone)
        notify_class_path: Full class path (preferred)
        track_index: Notify track index (used only if track_name is empty)
        track_name: Notify track name; resolves to index. Wins over track_index.
        tick_type: "Queued" (default) or "BranchingPoint"
        end_trigger_time_offset: Seconds added to the state-notify end trigger
        trigger_weight_threshold: Min animation weight for the notify to fire
        notify_trigger_chance: 0..1 probability the notify fires each evaluation
        trigger_on_dedicated_server: If false, skip on dedicated servers
        trigger_on_follower: If true, fires even when the animation is a sync-group follower
        properties: JSON dict of UPROPERTYs to set on the notify instance
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "notify_name": notify_name,
            "trigger_time": trigger_time,
        }
        if duration > 0:
            params["duration"] = duration
        if notify_class_path:
            params["notify_class_path"] = notify_class_path
        elif notify_class:
            params["notify_class"] = notify_class
        if track_name:
            params["track_name"] = track_name
        else:
            params["track_index"] = track_index
        if tick_type:
            params["tick_type"] = tick_type
        if end_trigger_time_offset is not None:
            params["end_trigger_time_offset"] = end_trigger_time_offset
        if trigger_weight_threshold is not None:
            params["trigger_weight_threshold"] = trigger_weight_threshold
        if notify_trigger_chance is not None:
            params["notify_trigger_chance"] = notify_trigger_chance
        if trigger_on_dedicated_server is not None:
            params["trigger_on_dedicated_server"] = trigger_on_dedicated_server
        if trigger_on_follower is not None:
            params["trigger_on_follower"] = trigger_on_follower
        if properties is not None:
            params["properties"] = properties
        response = unreal.send_command("add_montage_notify", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_montage_notify error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_montage_notify(
    asset_path: str,
    notify_name: str = "",
    notify_index: int = -1,
    trigger_time: Optional[float] = None,
    trigger_time_tolerance: float = 1e-4,
    remove_all: bool = False
) -> Dict[str, Any]:
    """Remove notifies from a montage by name (+ optional time) or index.

    Provide either notify_name or notify_index. When removing by name,
    pass trigger_time to disambiguate duplicates, or remove_all=True to
    delete every matching name.

    Args:
        asset_path: Path to the AnimMontage asset
        notify_name: Name of the notify to remove
        notify_index: Index in the Notifies array (ignored if < 0)
        trigger_time: Optional time-based disambiguator when matching by name
        trigger_time_tolerance: Seconds tolerance on trigger_time match (default 1e-4)
        remove_all: If true, remove every notify matching name (+ time if set)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if notify_name:
            params["notify_name"] = notify_name
        if notify_index >= 0:
            params["notify_index"] = notify_index
        if trigger_time is not None:
            params["trigger_time"] = trigger_time
            params["trigger_time_tolerance"] = trigger_time_tolerance
        if remove_all:
            params["remove_all"] = True
        response = unreal.send_command("remove_montage_notify", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_montage_notify error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_montage_notify(
    asset_path: str,
    notify_name: str = "",
    notify_index: int = -1,
    trigger_time: Optional[float] = None,
    trigger_time_tolerance: float = 1e-4,
    new_notify_name: str = "",
    new_trigger_time: Optional[float] = None,
    duration: Optional[float] = None,
    track_index: Optional[int] = None,
    track_name: str = "",
    tick_type: str = "",
    end_trigger_time_offset: Optional[float] = None,
    trigger_weight_threshold: Optional[float] = None,
    notify_trigger_chance: Optional[float] = None,
    trigger_on_dedicated_server: Optional[bool] = None,
    trigger_on_follower: Optional[bool] = None,
    properties: Optional[Dict[str, Any]] = None
) -> Dict[str, Any]:
    """Update an existing notify in place without reinstantiating its typed
    notify/notify-state object. Locate by notify_index, or by
    {notify_name, trigger_time}. Any non-None/non-empty field is applied.
    properties is merged onto the existing notify instance.

    Changing notify_class requires remove+add; this tool does not support it.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if notify_index >= 0:
            params["notify_index"] = notify_index
        else:
            if not notify_name:
                return {"success": False, "message": "Provide notify_index or notify_name"}
            params["notify_name"] = notify_name
            if trigger_time is not None:
                params["trigger_time"] = trigger_time
                params["trigger_time_tolerance"] = trigger_time_tolerance
        if new_notify_name:
            params["new_notify_name"] = new_notify_name
        if new_trigger_time is not None:
            params["new_trigger_time"] = new_trigger_time
        if duration is not None:
            params["duration"] = duration
        if track_name:
            params["track_name"] = track_name
        elif track_index is not None:
            params["track_index"] = track_index
        if tick_type:
            params["tick_type"] = tick_type
        if end_trigger_time_offset is not None:
            params["end_trigger_time_offset"] = end_trigger_time_offset
        if trigger_weight_threshold is not None:
            params["trigger_weight_threshold"] = trigger_weight_threshold
        if notify_trigger_chance is not None:
            params["notify_trigger_chance"] = notify_trigger_chance
        if trigger_on_dedicated_server is not None:
            params["trigger_on_dedicated_server"] = trigger_on_dedicated_server
        if trigger_on_follower is not None:
            params["trigger_on_follower"] = trigger_on_follower
        if properties is not None:
            params["properties"] = properties
        response = unreal.send_command("update_montage_notify", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_montage_notify error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_montage_notify_track(
    asset_path: str,
    track_name: str,
    color: Optional[Dict[str, float]] = None
) -> Dict[str, Any]:
    """Append a new notify track to a montage. Fails if the name already exists.

    color: optional { "r": float, "g": float, "b": float, "a": float } in [0, 1].
        Defaults to white.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "track_name": track_name,
        }
        if color is not None:
            params["color"] = color
        response = unreal.send_command("add_montage_notify_track", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_montage_notify_track error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_notify_track_color(
    asset_path: str,
    color: Dict[str, float],
    track_name: str = "",
    track_index: int = -1
) -> Dict[str, Any]:
    """Set the display color of a notify track.

    color: { "r": float, "g": float, "b": float, "a": float } in [0, 1].
    Identify the track by track_name or track_index.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path, "color": color}
        if track_name:
            params["track_name"] = track_name
        elif track_index >= 0:
            params["track_index"] = track_index
        else:
            return {"success": False, "message": "Provide track_name or track_index"}
        response = unreal.send_command("set_montage_notify_track_color", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_notify_track_color error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_time_stretch(
    asset_path: str,
    time_stretch_curve_name: str = "",
    sampling_rate: Optional[float] = None,
    curve_value_min_precision: Optional[float] = None
) -> Dict[str, Any]:
    """Configure a montage's time stretch curve.

    time_stretch_curve_name: name of a float curve authored on this montage that
        drives non-uniform time scaling. Pass "" or "None" to clear.
    sampling_rate: float curve sampling rate (default 60.0).
    curve_value_min_precision: minimum delta between consecutive sampled
        segments before combining (default 0.01).

    The actual curve data must be authored via add_anim_curve / set_anim_curve_keys
    on this montage; this tool only names the curve and tunes sampling.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if time_stretch_curve_name is not None:
            # Accept empty string to clear, so always forward the key
            params["time_stretch_curve_name"] = time_stretch_curve_name
        stretch_obj: Dict[str, Any] = {}
        if sampling_rate is not None:
            stretch_obj["SamplingRate"] = sampling_rate
        if curve_value_min_precision is not None:
            stretch_obj["CurveValueMinPrecision"] = curve_value_min_precision
        if stretch_obj:
            params["time_stretch_curve"] = stretch_obj
        response = unreal.send_command("set_montage_time_stretch", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_time_stretch error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_preview_base_pose(
    asset_path: str,
    preview_base_pose_path: str
) -> Dict[str, Any]:
    """Set the additive base pose used for in-editor preview. Editor-only.

    preview_base_pose_path: full path to a UAnimSequence, or "None" / "" to clear.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_montage_preview_base_pose", {
            "asset_path": asset_path,
            "preview_base_pose_path": preview_base_pose_path,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_preview_base_pose error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_playback(
    asset_path: str,
    rate_scale: Optional[float] = None,
    loop: Optional[bool] = None
) -> Dict[str, Any]:
    """Set default playback rate and loop flag on a montage.

    rate_scale: default playback rate multiplier (1.0 = normal).
    loop: default looping behavior; asset players can override.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if rate_scale is not None:
            params["rate_scale"] = rate_scale
        if loop is not None:
            params["loop"] = loop
        if len(params) == 1:
            return {"success": False, "message": "Provide rate_scale or loop"}
        response = unreal.send_command("set_montage_playback", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_playback error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_sync_group(
    asset_path: str,
    sync_group: str = "",
    sync_slot_index: Optional[int] = None
) -> Dict[str, Any]:
    """Configure the montage sync group for marker-based sync.

    sync_group: FName of the sync group ("" or "None" clears).
    sync_slot_index: Index of the slot that collects sync markers.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if sync_group:
            params["sync_group"] = sync_group
        if sync_slot_index is not None:
            params["sync_slot_index"] = sync_slot_index
        if len(params) == 1:
            return {"success": False, "message": "Provide sync_group or sync_slot_index"}
        response = unreal.send_command("set_montage_sync_group", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_sync_group error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_section_metadata(
    asset_path: str,
    section_name: str,
    metadata_class_path: str,
    properties: Optional[Dict[str, Any]] = None
) -> Dict[str, Any]:
    """Append a UAnimMetaData instance to a composite section.

    metadata_class_path: full path to a UAnimMetaData subclass.
    properties: dict of UPROPERTYs to apply on the instance.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "section_name": section_name,
            "metadata_class_path": metadata_class_path,
        }
        if properties is not None:
            params["properties"] = properties
        response = unreal.send_command("add_section_metadata", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_section_metadata error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_section_metadata(
    asset_path: str,
    section_name: str,
    metadata_index: int
) -> Dict[str, Any]:
    """Remove a UAnimMetaData instance from a section by index."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_section_metadata", {
            "asset_path": asset_path,
            "section_name": section_name,
            "metadata_index": metadata_index,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_section_metadata error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reorder_montage_section(
    asset_path: str,
    from_index: int,
    to_index: int
) -> Dict[str, Any]:
    """Move a composite section from one array position to another."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("reorder_montage_section", {
            "asset_path": asset_path,
            "from_index": from_index,
            "to_index": to_index,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reorder_montage_section error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reorder_montage_segment(
    asset_path: str,
    from_index: int,
    to_index: int,
    slot_index: int = -1,
    slot_name: str = ""
) -> Dict[str, Any]:
    """Move a segment within a slot from one array position to another.
    Identify the slot by slot_name or slot_index.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "from_index": from_index,
            "to_index": to_index,
        }
        if slot_name:
            params["slot_name"] = slot_name
        elif slot_index >= 0:
            params["slot_index"] = slot_index
        else:
            return {"success": False, "message": "Provide slot_name or slot_index"}
        response = unreal.send_command("reorder_montage_segment", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reorder_montage_segment error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reorder_montage_notify_track(
    asset_path: str,
    from_index: int,
    to_index: int
) -> Dict[str, Any]:
    """Move a notify track. Every notify's track_index reference is remapped
    automatically so notifies follow their track to the new position.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("reorder_montage_notify_track", {
            "asset_path": asset_path,
            "from_index": from_index,
            "to_index": to_index,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reorder_montage_notify_track error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reorder_montage_slot(
    asset_path: str,
    from_index: int,
    to_index: int
) -> Dict[str, Any]:
    """Move a slot track. The montage sync_slot_index is remapped automatically
    so the sync slot follows the move.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("reorder_montage_slot", {
            "asset_path": asset_path,
            "from_index": from_index,
            "to_index": to_index,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reorder_montage_slot error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_montage_notify_track(
    asset_path: str,
    new_name: str,
    track_name: str = "",
    track_index: int = -1
) -> Dict[str, Any]:
    """Rename a notify track. Identify it by track_name or track_index."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path, "new_name": new_name}
        if track_name:
            params["track_name"] = track_name
        elif track_index >= 0:
            params["track_index"] = track_index
        else:
            return {"success": False, "message": "Provide track_name or track_index"}
        response = unreal.send_command("rename_montage_notify_track", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"rename_montage_notify_track error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_montage_notify_track(
    asset_path: str,
    track_name: str = "",
    track_index: int = -1,
    reassign_to: int = -1
) -> Dict[str, Any]:
    """Remove a notify track. If any notify still references it,
    pass reassign_to to relocate them to another track index first.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if track_name:
            params["track_name"] = track_name
        elif track_index >= 0:
            params["track_index"] = track_index
        else:
            return {"success": False, "message": "Provide track_name or track_index"}
        if reassign_to >= 0:
            params["reassign_to"] = reassign_to
        response = unreal.send_command("remove_montage_notify_track", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_montage_notify_track error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_montage_segment(
    asset_path: str,
    animation_path: str,
    slot_index: int = 0,
    slot_name: str = "",
    create_slot_if_missing: bool = True,
    start_pos: float = 0.0,
    anim_start_time: float = 0.0,
    anim_end_time: float = -1.0,
    play_rate: float = 1.0,
    looping_count: int = 1
) -> Dict[str, Any]:
    """Add an animation segment to a slot track on a montage.

    Args:
        asset_path: Path to the AnimMontage asset
        animation_path: Path to the AnimSequence to add
        slot_index: Index of the slot track (used if slot_name is empty)
        slot_name: Slot track name (preferred over slot_index)
        create_slot_if_missing: If slot_name doesn't exist, create it (default true)
        start_pos: Position in the montage timeline where the segment starts
        anim_start_time: Start time within the source animation
        anim_end_time: End time within the source animation (-1 = full length)
        play_rate: Playback rate (default 1.0)
        looping_count: How many times the segment's anim loops within this slot (default 1)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "animation_path": animation_path,
            "start_pos": start_pos,
            "anim_start_time": anim_start_time,
            "play_rate": play_rate,
            "looping_count": looping_count,
            "create_slot_if_missing": create_slot_if_missing,
        }
        if slot_name:
            params["slot_name"] = slot_name
        else:
            params["slot_index"] = slot_index
        if anim_end_time >= 0:
            params["anim_end_time"] = anim_end_time
        response = unreal.send_command("add_montage_segment", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_montage_segment error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_montage_segment(
    asset_path: str,
    segment_index: int,
    slot_index: int = -1,
    slot_name: str = ""
) -> Dict[str, Any]:
    """Remove a segment from a montage slot. Composite length is recomputed."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "segment_index": segment_index,
        }
        if slot_name:
            params["slot_name"] = slot_name
        elif slot_index >= 0:
            params["slot_index"] = slot_index
        else:
            return {"success": False, "message": "Provide slot_name or slot_index"}
        response = unreal.send_command("remove_montage_segment", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_montage_segment error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_montage_segment(
    asset_path: str,
    segment_index: int,
    slot_index: int = -1,
    slot_name: str = "",
    animation_path: str = "",
    start_pos: Optional[float] = None,
    anim_start_time: Optional[float] = None,
    anim_end_time: Optional[float] = None,
    play_rate: Optional[float] = None,
    looping_count: Optional[int] = None
) -> Dict[str, Any]:
    """Update fields on an existing montage segment in place.

    Any non-None / non-empty value is applied. Composite length is recomputed.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "segment_index": segment_index,
        }
        if slot_name:
            params["slot_name"] = slot_name
        elif slot_index >= 0:
            params["slot_index"] = slot_index
        else:
            return {"success": False, "message": "Provide slot_name or slot_index"}
        if animation_path:
            params["animation_path"] = animation_path
        if start_pos is not None:
            params["start_pos"] = start_pos
        if anim_start_time is not None:
            params["anim_start_time"] = anim_start_time
        if anim_end_time is not None:
            params["anim_end_time"] = anim_end_time
        if play_rate is not None:
            params["play_rate"] = play_rate
        if looping_count is not None:
            params["looping_count"] = looping_count
        response = unreal.send_command("update_montage_segment", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_montage_segment error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_montage_section(
    asset_path: str,
    section_name: str,
    new_name: str = "",
    start_time: Optional[float] = None,
    next_section_name: str = "",
    link_method: str = ""
) -> Dict[str, Any]:
    """Update a composite section on a montage.

    Any non-empty/non-None value is applied. If new_name is provided, all
    sections whose NextSectionName pointed at the old name are rewired.

    link_method: "Absolute" | "Relative" | "Proportional"
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "section_name": section_name,
        }
        if new_name:
            params["new_name"] = new_name
        if start_time is not None:
            params["start_time"] = start_time
        if next_section_name:
            params["next_section_name"] = next_section_name
        if link_method:
            params["link_method"] = link_method
        response = unreal.send_command("update_montage_section", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_montage_section error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_blend_times(
    asset_path: str,
    blend_in_time: float = -1.0,
    blend_out_time: float = -1.0,
    blend_in_mode: str = "",
    blend_out_mode: str = "",
    blend_out_trigger_time: Optional[float] = None,
    enable_auto_blend_out: Optional[bool] = None,
    blend_profile_in_path: str = "",
    blend_profile_out_path: str = "",
    blend_mode_in: str = "",
    blend_mode_out: str = ""
) -> Dict[str, Any]:
    """Set blend settings on an existing montage.

    Any unspecified value is left unchanged.

    blend_in_mode / blend_out_mode (curve shape):
        "Linear" | "Cubic" | "HermiteCubic" | "Sinusoidal" |
        "QuadraticInOut" | "CubicInOut" | "QuarticInOut" | "QuinticInOut" |
        "CircularIn" | "CircularOut" | "CircularInOut" |
        "ExpIn" | "ExpOut" | "ExpInOut" | "Custom"

    blend_mode_in / blend_mode_out (blend technique):
        "Standard" | "Inertialization". Inertialization requires an
        inertialization node downstream of the slot in the AnimBP.

    blend_out_trigger_time: seconds from montage end at which blend-out begins.
        <0 means "use blend_out_time so blend finishes exactly as the montage ends".

    blend_profile_in_path / blend_profile_out_path: full path to a UBlendProfile.
        Pass "None" to clear; empty string means unchanged.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if blend_in_time >= 0:
            params["blend_in_time"] = blend_in_time
        if blend_out_time >= 0:
            params["blend_out_time"] = blend_out_time
        if blend_in_mode:
            params["blend_in_mode"] = blend_in_mode
        if blend_out_mode:
            params["blend_out_mode"] = blend_out_mode
        if blend_out_trigger_time is not None:
            params["blend_out_trigger_time"] = blend_out_trigger_time
        if enable_auto_blend_out is not None:
            params["enable_auto_blend_out"] = enable_auto_blend_out
        if blend_profile_in_path:
            params["blend_profile_in_path"] = blend_profile_in_path
        if blend_profile_out_path:
            params["blend_profile_out_path"] = blend_profile_out_path
        if blend_mode_in:
            params["blend_mode_in"] = blend_mode_in
        if blend_mode_out:
            params["blend_mode_out"] = blend_mode_out
        response = unreal.send_command("set_montage_blend_times", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_blend_times error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_montage_root_motion(
    asset_path: str,
    root_motion_root_lock: str = "",
    enable_root_motion_translation: Optional[bool] = None,
    enable_root_motion_rotation: Optional[bool] = None
) -> Dict[str, Any]:
    """Configure root-motion behavior on a montage.

    root_motion_root_lock: "RefPose" | "AnimFirstFrame" | "Zero".
        Controls what transform the root bone is locked to when extracting root motion.
    enable_root_motion_translation / enable_root_motion_rotation: montage-level flags
        (note: these are the legacy montage flags; modern root-motion extraction is
        primarily driven by the source anim sequence — see set_anim_sequence_root_motion).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if root_motion_root_lock:
            params["root_motion_root_lock"] = root_motion_root_lock
        if enable_root_motion_translation is not None:
            params["enable_root_motion_translation"] = enable_root_motion_translation
        if enable_root_motion_rotation is not None:
            params["enable_root_motion_rotation"] = enable_root_motion_rotation
        response = unreal.send_command("set_montage_root_motion", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_montage_root_motion error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_anim_sequence_root_motion(
    asset_path: str,
    enable_root_motion: Optional[bool] = None,
    root_motion_root_lock: str = "",
    force_root_lock: Optional[bool] = None,
    use_normalized_root_motion_scale: Optional[bool] = None
) -> Dict[str, Any]:
    """Configure root-motion flags on a UAnimSequence asset.

    These are the flags that actually drive modern root-motion extraction — the ones
    flipped in the AnimSequence editor's "Root Motion" details panel. Flip these on
    attack / hit-reaction sequences to enable dynamic push-back behavior.

    Fields:
        enable_root_motion:                 Master enable for root-motion extraction.
        root_motion_root_lock:              "RefPose" | "AnimFirstFrame" | "Zero".
                                            Controls what transform the root bone is
                                            locked to when extracting root motion.
        force_root_lock:                    Force root-bone lock even when root motion
                                            is disabled (for in-place playback).
        use_normalized_root_motion_scale:   Extract root motion with FVector(1,1,1) scale
                                            regardless of authored mesh scale.

    Any unspecified value is left unchanged. Requires at least one field.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if enable_root_motion is not None:
            params["enable_root_motion"] = enable_root_motion
        if root_motion_root_lock:
            params["root_motion_root_lock"] = root_motion_root_lock
        if force_root_lock is not None:
            params["force_root_lock"] = force_root_lock
        if use_normalized_root_motion_scale is not None:
            params["use_normalized_root_motion_scale"] = use_normalized_root_motion_scale
        response = unreal.send_command("set_anim_sequence_root_motion", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_anim_sequence_root_motion error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_anim_sequence_properties(
    asset_path: str,
    interpolation: str = "",
    additive_anim_type: str = "",
    ref_pose_type: str = "",
    ref_pose_seq: Optional[str] = None,
    ref_frame_index: Optional[int] = None,
    bone_compression_settings: Optional[str] = None,
    curve_compression_settings: Optional[str] = None,
    variable_frame_stripping_settings: Optional[str] = None
) -> Dict[str, Any]:
    """Configure interpolation / additive / compression-reference fields on a UAnimSequence.

    Orthogonal to set_anim_sequence_root_motion — these are the AnimSequence detail-panel
    fields that influence pose evaluation (not just root-motion extraction). A sequence
    mis-tagged as additive will drift visibly; a sequence with the wrong base-pose type
    will produce bent / broken poses when compiled via the compression pipeline.

    Fields:
        interpolation:                     "Linear" | "Step". How values are evaluated
                                           between keys.
        additive_anim_type:                "None" | "LocalSpaceBase" | "RotationOffsetMeshSpace".
                                           Base type for additive blending.
        ref_pose_type:                     "None" | "RefPose" | "AnimScaled" | "AnimFrame" |
                                           "LocalAnimFrame". How the additive reference
                                           pose is sourced.
        ref_pose_seq:                      Asset path to the UAnimSequence used as the
                                           additive base (or "" / "None" to clear).
                                           Required when ref_pose_type is AnimScaled or
                                           AnimFrame.
        ref_frame_index:                   Frame index into ref_pose_seq when
                                           ref_pose_type is AnimFrame / LocalAnimFrame.
        bone_compression_settings:         Asset path to UAnimBoneCompressionSettings
                                           (or "" / "None" to clear).
        curve_compression_settings:        Asset path to UAnimCurveCompressionSettings
                                           (or "" / "None" to clear).
        variable_frame_stripping_settings: Asset path to UVariableFrameStrippingSettings
                                           (or "" / "None" to clear).

    Any unspecified value is left unchanged. Requires at least one field. Empty string
    or "None" explicitly clears an asset reference.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if interpolation:
            params["interpolation"] = interpolation
        if additive_anim_type:
            params["additive_anim_type"] = additive_anim_type
        if ref_pose_type:
            params["ref_pose_type"] = ref_pose_type
        if ref_pose_seq is not None:
            params["ref_pose_seq"] = ref_pose_seq
        if ref_frame_index is not None:
            params["ref_frame_index"] = ref_frame_index
        if bone_compression_settings is not None:
            params["bone_compression_settings"] = bone_compression_settings
        if curve_compression_settings is not None:
            params["curve_compression_settings"] = curve_compression_settings
        if variable_frame_stripping_settings is not None:
            params["variable_frame_stripping_settings"] = variable_frame_stripping_settings
        response = unreal.send_command("set_anim_sequence_properties", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_anim_sequence_properties error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_montage_slot(
    asset_path: str,
    slot_name: str
) -> Dict[str, Any]:
    """Add a new slot track to a montage. Fails if a slot with this name exists."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_montage_slot", {
            "asset_path": asset_path,
            "slot_name": slot_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_montage_slot error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_montage_slot(
    asset_path: str,
    new_slot_name: str,
    slot_name: str = "",
    slot_index: int = -1
) -> Dict[str, Any]:
    """Rename a slot track. Identify it by slot_name or slot_index."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path, "new_slot_name": new_slot_name}
        if slot_name:
            params["slot_name"] = slot_name
        elif slot_index >= 0:
            params["slot_index"] = slot_index
        else:
            return {"success": False, "message": "Provide slot_name or slot_index"}
        response = unreal.send_command("rename_montage_slot", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"rename_montage_slot error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_montage_slot(
    asset_path: str,
    slot_name: str = "",
    slot_index: int = -1,
    force: bool = False
) -> Dict[str, Any]:
    """Remove a slot track (and its segments). force=true is required when the
    slot still contains segments. The last remaining slot on a montage cannot be
    removed.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"asset_path": asset_path}
        if slot_name:
            params["slot_name"] = slot_name
        elif slot_index >= 0:
            params["slot_index"] = slot_index
        else:
            return {"success": False, "message": "Provide slot_name or slot_index"}
        if force:
            params["force"] = True
        response = unreal.send_command("remove_montage_slot", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_montage_slot error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================
# Animation Curve & Root Motion Tools
# ============================================================

@mcp.tool()
def get_anim_curve_keys(asset_path: str, curve_name: str) -> Dict[str, Any]:
    """Read all keyframes from a named float curve on an animation sequence.

    Returns each key's time, value, interpolation mode, and tangent data.

    Args:
        asset_path: Full asset path to the AnimSequence (e.g., "/Game/Animations/AS_Walk")
        curve_name: Name of the float curve to read (e.g., "Speed")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_anim_curve_keys", {
            "asset_path": asset_path,
            "curve_name": curve_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_anim_curve_keys error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_root_motion_data(asset_path: str, num_samples: int = 10) -> Dict[str, Any]:
    """Extract root motion translation data from an animation sequence.

    Returns total distance, average speed (distance/playLength), and optional
    per-sample distance curve. Useful for calculating Speed curve values for
    stride warping systems (StrideWarpRatio = ActualGroundSpeed / SpeedCurveValue).

    Args:
        asset_path: Full asset path to the AnimSequence
        num_samples: Number of evenly-spaced samples for the distance curve (default: 10)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_root_motion_data", {
            "asset_path": asset_path,
            "num_samples": num_samples
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_root_motion_data error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_anim_curve(
    asset_path: str,
    curve_name: str,
    keys: List[Dict[str, Any]]
) -> Dict[str, Any]:
    """Add a new named float curve with keyframes to an animation sequence.

    Creates a new curve. Fails if the curve already exists (use set_anim_curve_keys
    to modify existing curves).

    For a constant Speed curve, use two keys:
      [{"time": 0, "value": 150.0, "interp_mode": "Constant"},
       {"time": 1.2, "value": 150.0, "interp_mode": "Constant"}]

    Args:
        asset_path: Full asset path to the AnimSequence
        curve_name: Name for the new curve (e.g., "Speed")
        keys: List of keyframe dicts, each with "time" (float), "value" (float),
              and optional "interp_mode" ("Constant", "Linear", or "Cubic", default: "Linear")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_anim_curve", {
            "asset_path": asset_path,
            "curve_name": curve_name,
            "keys": keys
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_anim_curve error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_anim_curve_keys(
    asset_path: str,
    curve_name: str,
    keys: List[Dict[str, Any]]
) -> Dict[str, Any]:
    """Replace all keyframes on an existing float curve of an animation sequence.

    The curve must already exist. Use add_anim_curve to create new curves.

    Args:
        asset_path: Full asset path to the AnimSequence
        curve_name: Name of the existing curve to modify
        keys: List of keyframe dicts (same format as add_anim_curve)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_anim_curve_keys", {
            "asset_path": asset_path,
            "curve_name": curve_name,
            "keys": keys
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_anim_curve_keys error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_anim_curve(asset_path: str, curve_name: str) -> Dict[str, Any]:
    """Remove a named float curve from an animation sequence entirely.

    Args:
        asset_path: Full asset path to the AnimSequence
        curve_name: Name of the curve to remove
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_anim_curve", {
            "asset_path": asset_path,
            "curve_name": curve_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_anim_curve error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def batch_add_speed_curves(
    directory: str,
    curve_name: str = "Speed",
    overwrite: bool = True,
    dry_run: bool = False,
    mode: str = "auto",
    sample_rate_fps: float = 30.0,
    flat_tolerance_cm_s: float = 1.5,
    flat_tolerance_fraction: float = 0.03
) -> Dict[str, Any]:
    """Batch-add Speed float curves to all animation sequences in a folder.

    For each AnimSequence found:
    1. Extracts root motion per-frame via centered windowing.
    2. Computes instantaneous horizontal speed at each sample.
    3. Authors linear-interpolated keys — so Start/End (ramp) clips get accurate
       per-frame speed, and uniform loops automatically collapse to a flat
       2-key constant curve (within tolerance).

    Designed for locomotion animations used with GMCE stride warping,
    where StrideWarpRatio = ActualGroundSpeed / SpeedCurveValue.

    Args:
        directory: Content folder to scan (e.g., "/Game/PRK/Character/Anims/Locomotion/")
        curve_name: Name of the speed curve (default: "Speed")
        overwrite: If True, update existing curves. If False, skip them. (default: True)
        dry_run: If True, report what would be done without modifying anything (default: False)
        mode: "auto" (per-frame, collapses uniform clips to flat), "per_frame" (always ramped),
              or "constant" (legacy 2-key flat curve at clip average). Default: "auto".
        sample_rate_fps: Sampling rate for per-frame mode. The sequence's own sampling rate is
            used if higher. Clamped to [10, 120]. Default: 30.
        flat_tolerance_cm_s: Absolute spread tolerance (max - min sampled speed) for auto-collapse
            to flat. Default: 1.5 cm/s.
        flat_tolerance_fraction: Fractional tolerance vs. average speed for auto-collapse.
            Effective tolerance is max(flat_tolerance_cm_s, flat_tolerance_fraction * avg).
            Default: 0.03 (3%).

    Returns:
        Per-asset results include `curve_mode` ("flat" | "per_frame" | "constant"),
        `sample_rate_fps`, `min_sampled_speed`, `max_sampled_speed`, `num_keys`.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("batch_add_speed_curves", {
            "directory": directory,
            "curve_name": curve_name,
            "overwrite": overwrite,
            "dry_run": dry_run,
            "mode": mode,
            "sample_rate_fps": sample_rate_fps,
            "flat_tolerance_cm_s": flat_tolerance_cm_s,
            "flat_tolerance_fraction": flat_tolerance_fraction
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"batch_add_speed_curves error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================
# Mirror Data Table Tools
# ============================================================

@mcp.tool()
def create_mirror_data_table(
    asset_path: str,
    skeleton_path: str,
    expressions: Optional[List[Dict[str, str]]] = None,
    mirror_axis: str = "X",
    mirror_root_motion: bool = True
) -> Dict[str, Any]:
    """Create a Mirror Data Table asset for animation mirroring (left/right hand swap).

    Auto-populates bone pair mappings from the skeleton using find/replace expressions.
    Default expressions use _l/_r suffix convention (e.g., hand_l <-> hand_r).

    Args:
        asset_path: Where to create the asset (e.g., "/Game/PRK/Animation/MirrorTables/MDT_PRKSkeleton")
        skeleton_path: Path to the USkeleton or USkeletalMesh asset
        expressions: Optional list of find/replace dicts. Each dict has:
            "find" (str), "replace" (str), "method" ("Prefix", "Suffix", or "Regex").
            Default: [{"find": "_l", "replace": "_r", "method": "Suffix"},
                      {"find": "_r", "replace": "_l", "method": "Suffix"}]
        mirror_axis: Axis for mirroring ("X", "Y", or "Z", default: "X" for left/right)
        mirror_root_motion: Whether to mirror root motion (default: True)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "skeleton_path": skeleton_path,
            "mirror_axis": mirror_axis,
            "mirror_root_motion": mirror_root_motion
        }
        if expressions is not None:
            params["expressions"] = expressions
        response = unreal.send_command("create_mirror_data_table", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_mirror_data_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def analyze_mirror_data_table(asset_path: str) -> Dict[str, Any]:
    """Analyze an existing Mirror Data Table asset.

    Returns the skeleton, mirror axis, find/replace expressions, and all bone/curve/notify
    pair mappings (rows).

    Args:
        asset_path: Full asset path to the UMirrorDataTable
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("analyze_mirror_data_table", {
            "asset_path": asset_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_mirror_data_table error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================
# AnimBlueprint Tools
# ============================================================

@mcp.tool()
def add_anim_node(
    asset_path: str,
    node_type: str,
    pos_x: int = 0,
    pos_y: int = 0,
    state_machine: str = "",
    state: str = "",
    **kwargs
) -> Dict[str, Any]:
    """Add an AnimGraph node to an Animation Blueprint.

    By default the node lands in the top-level AnimGraph. Pass `state_machine`
    + `state` to instead add the node to that state's inner pose graph
    (UAnimStateNode::BoundGraph) — required for editing what plays inside a
    state without using the editor UI.

    Supported node_type values:
    - "CopyPoseFromMesh" - Copy pose from attached skeletal mesh
      - use_attached_parent (bool), copy_curves (bool)
    - "AnimDynamics" - Physics-based animation dynamics
      - bound_bone (str), chain_end (str), gravity_scale (float)
    - "RigidBody" - RBAN immediate-mode physics simulation (dangling gear, chains)
      - physics_asset (str, optional override path), simulation_space
        (str: "ComponentSpace"|"WorldSpace"|"BaseBoneSpace"),
        override_world_gravity_z (float)
    - "ApplyAdditive" - Apply additive animation layer
      - alpha (float, 0-1)
    - "DeadBlending" - Inertialization/dead blending transition node
    - "Slot" - Animation slot node for montage playback
      - slot_name (str, e.g., "DefaultSlot")
    - "LayeredBoneBlend" - Per-bone layer blending
    - "SaveCachedPose" - Save a cached pose for reuse
      - cache_name (str)
    - "UseCachedPose" - Reference a saved cached pose
    - "Mirror" - Mirror node for offhand / left-right pose flipping
      - mirror_data_table_path (str, optional asset path, e.g.
        "/Game/PRK/Animation/MirrorTables/MDT_PRKSkeleton.MDT_PRKSkeleton"
        — pre-binds FAnimNode_Mirror.MirrorDataTable on creation)
    - "OrientationWarping" - Orientation warping (AnimationWarping plugin)
      - rotation_axis (str: "X","Y","Z"), rotation_interp_speed (float),
        distributed_bone_orientation_alpha (float), spine_bones (list[str]),
        ik_foot_root_bone (str), ik_foot_bones (list[str])
    - "StrideWarping" - Stride warping for locomotion (AnimationWarping plugin)
      - pelvis_bone (str), ik_foot_root_bone (str), stride_scale (float),
        foot_definitions (list of {ik_foot_bone, fk_foot_bone, thigh_bone})

    Returns the node_id and pin list for use with connect_anim_nodes /
    delete_anim_node.

    Args:
        asset_path: Full asset path to the AnimBlueprint
        node_type: The type of AnimGraph node to create
        pos_x: X position in the graph (default: 0)
        pos_y: Y position in the graph (default: 0)
        state_machine: Name of state machine that owns the target state
            (optional; required when `state` is set)
        state: Name of the state whose inner pose graph the node should be
            added to (optional; defaults to top-level AnimGraph)
        **kwargs: Node-type-specific properties (see above)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "node_type": node_type,
            "pos_x": pos_x,
            "pos_y": pos_y
        }
        if state_machine:
            params["state_machine"] = state_machine
        if state:
            params["state"] = state
        params.update(kwargs)
        response = unreal.send_command("add_anim_node", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_anim_node error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_anim_node(
    asset_path: str,
    node_id: Any,
    state_machine: str = "",
    state: str = ""
) -> Dict[str, Any]:
    """Delete an AnimGraph node from an Animation Blueprint.

    Counterpart to add_anim_node. Use the `node_id` returned by add_anim_node
    or read_anim_blueprint. Defaults to the top-level AnimGraph; supply
    `state_machine` + `state` to delete from a state's inner pose graph
    (UAnimStateNode::BoundGraph). Breaks all links before removal.

    Note: the generic `delete_node` tool matches by NodeGuid/Name and only
    looks in the EventGraph or function graphs, so it cannot delete anim
    nodes returned by add_anim_node. Use this tool instead.

    Args:
        asset_path: Full asset path to the AnimBlueprint
        node_id: ID returned by add_anim_node (int UniqueID), or a NodeGuid /
            node name string
        state_machine: Name of state machine that owns the target state
            (optional; required when `state` is set)
        state: Name of the state whose inner pose graph holds the node
            (optional; defaults to top-level AnimGraph)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "node_id": str(node_id),
        }
        if state_machine:
            params["state_machine"] = state_machine
        if state:
            params["state"] = state
        response = unreal.send_command("delete_anim_node", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_anim_node error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def connect_anim_nodes(
    asset_path: str,
    source_node_id: Any,
    source_pin: str,
    target_node_id: Any,
    target_pin: str
) -> Dict[str, Any]:
    """Connect two AnimGraph nodes by their pins.

    Use the node_id and pin names returned by add_anim_node or read_anim_blueprint.
    Pose output pins connect to pose input pins (e.g., "Output Pose" -> "Source").

    Args:
        asset_path: Full asset path to the AnimBlueprint
        source_node_id: The ID of the source node (from add_anim_node response, string or int)
        source_pin: Name of the output pin on the source node
        target_node_id: The ID of the target node (string or int)
        target_pin: Name of the input pin on the target node
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("connect_anim_nodes", {
            "asset_path": asset_path,
            "source_node_id": str(source_node_id),
            "source_pin": source_pin,
            "target_node_id": str(target_node_id),
            "target_pin": target_pin
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"connect_anim_nodes error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def disconnect_anim_nodes(
    asset_path: str,
    source_node_id: Any,
    source_pin: str,
    target_node_id: Any,
    target_pin: str
) -> Dict[str, Any]:
    """Break a specific link between two AnimGraph pins.

    Counterpart to connect_anim_nodes. Required when inserting a node mid-chain
    so the old direct A->C link doesn't linger alongside the new A->B->C path.
    Returns `was_linked=False` if the link did not exist (operation is a no-op
    in that case, not an error).

    Args:
        asset_path: Full asset path to the AnimBlueprint
        source_node_id: The ID of the source node (string or int)
        source_pin: Name of the output pin on the source node
        target_node_id: The ID of the target node (string or int)
        target_pin: Name of the input pin on the target node
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("disconnect_anim_nodes", {
            "asset_path": asset_path,
            "source_node_id": str(source_node_id),
            "source_pin": source_pin,
            "target_node_id": str(target_node_id),
            "target_pin": target_pin
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"disconnect_anim_nodes error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_pin_default_value(
    asset_path: str,
    node_id: Any,
    pin_name: str,
    default_value: str
) -> Dict[str, Any]:
    """Set the literal default value on an unconnected input pin.

    Use cases:
    - Set a `K2Node_EnumEquality.B` pin to a literal enum value
      (default_value = "Aimed" sets EPRKBowPhase::Aimed without adding a literal node).
    - Set a scalar default on an AnimGraph node input pin without wiring a constant.
    - Set a struct default via UE's serialized form (e.g. "(X=1,Y=0,Z=0)" for a Vector).
    - Set an asset-pin default by passing the asset path; the resolved object is
      stored in DefaultObject.

    The pin must currently have NO connected sources (returns error if connected).
    Disconnect first via disconnect_anim_nodes if needed.

    Args:
        asset_path: Full asset path to the AnimBlueprint
        node_id: The ID of the node owning the pin (string or int)
        pin_name: Name of the input pin to set
        default_value: The default value as a string (enum name, scalar, struct
            literal, or asset path depending on pin type)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_pin_default_value", {
            "asset_path": asset_path,
            "node_id": str(node_id),
            "pin_name": pin_name,
            "default_value": default_value
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_pin_default_value error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_transition_property(
    asset_path: str,
    property_name: str,
    property_value: Any,
    state_machine_name: Optional[str] = None,
    from_state: Optional[str] = None,
    to_state: Optional[str] = None,
    transition_node_id: Optional[Any] = None
) -> Dict[str, Any]:
    """Set a scalar property on a state-machine transition node.

    Identify the transition either by `transition_node_id` (from
    read_anim_blueprint), or by `state_machine_name` + `from_state` + `to_state`.

    Supported property_name values (case-insensitive, both snake_case and PascalCase):
        - "crossfade_duration" / "CrossfadeDuration": float seconds
        - "priority" / "PriorityOrder": int (lower = higher priority)
        - "bidirectional" / "Bidirectional": bool
        - "disabled" / "bDisabled": bool
        - "automatic_rule_trigger_time" / "AutomaticRuleTriggerTime": float
        - "automatic_rule" / "bAutomaticRuleBasedOnSequencePlayerInState": bool

    Doesn't touch the rule graph — use simplify_transition_rule for that.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "property_name": property_name,
            "property_value": property_value,
        }
        if transition_node_id is not None:
            params["transition_node_id"] = str(transition_node_id)
        if state_machine_name:
            params["state_machine_name"] = state_machine_name
        if from_state:
            params["from_state"] = from_state
        if to_state:
            params["to_state"] = to_state
        response = unreal.send_command("set_transition_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_transition_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def simplify_transition_rule(
    asset_path: str,
    variable_name: str,
    state_machine_name: str = "",
    from_state: str = "",
    to_state: str = "",
    negate: bool = False,
    transition_node_id: str = ""
) -> Dict[str, Any]:
    """Replace a transition rule's bound graph with a single bool variable test.

    Common use: stripping `bool && elapsed_time > X` conjuncts down to just `bool`.
    Wipes everything in the rule's bound graph except the TransitionResult node,
    then adds a K2Node_VariableGet (and optionally a NOT_PreBool when `negate=True`)
    and connects to TransitionResult.bCanEnterTransition.

    The variable must exist on the AnimBP's generated class as a bool property
    (members from C++ AnimInstance parents work — e.g. bIsAirborne, bHasLanded).
    AnimBP must have been compiled at least once before this call.

    Identify the transition by `transition_node_id` (the `node_id` add_transition
    returns) OR by `state_machine_name` + `from_state` + `to_state`. The name form
    REFUSES when two transitions share the pair, because this call wipes the rule
    graph — use the node id to target the new one.

    Args:
        asset_path: Full asset path to the AnimBlueprint
        variable_name: Bool member to test (e.g. "bIsAirborne")
        state_machine_name: Name of the state machine containing the transition
        from_state: Source state name
        to_state: Target state name
        negate: If True, wrap the variable in NOT_PreBool ("NOT bIsGrounded" etc.)
        transition_node_id: Node id of the transition (takes precedence over names)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "variable_name": variable_name,
            "negate": negate,
        }
        if transition_node_id:
            params["transition_node_id"] = transition_node_id
        else:
            params["state_machine_name"] = state_machine_name
            params["from_state"] = from_state
            params["to_state"] = to_state
        response = unreal.send_command("simplify_transition_rule", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"simplify_transition_rule error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_anim_blueprint(asset_path: str) -> Dict[str, Any]:
    """Read and analyze an Animation Blueprint's structure.

    Returns all AnimGraph nodes, state machines, states, transitions, and their connections.

    Top-level fields worth calling out:
        root_motion_mode: "NoRootMotionExtraction" | "IgnoreRootMotion" |
            "RootMotionFromEverything" | "RootMotionFromMontagesOnly" — read from the
            AnimInstance CDO. For networked/GMC projects, anything other than
            RootMotionFromMontagesOnly is a common cause of locomotion skating.

        event_graph: K2 graphs outside the AnimGraph — Ubergraph pages (where
            BlueprintUpdateAnimation / BlueprintThreadSafeUpdateAnimation events
            live as overrides) and user-authored function graphs. Each entry
            exposes its starting K2Node_Event / K2Node_FunctionEntry plus a
            depth-limited exec_chain that follows the first exec output through
            CallFunction / Branch / Sequence chains. Branches recurse into both
            then/else and capture the condition_source node so callers can
            verify gating without a second query.

        inertialization_signal: Derived booleans confirming the standard
            Inertialization request handshake is wired:
              has_consume_call,
              has_request_call,
              consume_drives_branch_then_to_request.
            Returns the matching node_ids when found. Without this chain, an
            Inertialization node in the AnimGraph receives zero requests at runtime.

    Each entry in anim_graph_nodes (and states_detail[].graph_nodes) includes a
    `properties` object with reflected values from the inner FAnimNode_* struct
    (CPF_Edit-filtered). Use this to inspect non-pin configuration — e.g.
    StrideWarping's Mode (Graph|Manual), LocomotionSpeed, MinRootMotionSpeedThreshold,
    or BlendSpacePlayer's GroupName / GroupRole.

    Args:
        asset_path: Full asset path to the AnimBlueprint
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_anim_blueprint", {
            "asset_path": asset_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_anim_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_anim_blueprint(
    name: str,
    skeleton_path: str,
    path: str = "/Game/AnimBlueprints/",
    parent_class: str = "",
    preview_mesh: str = ""
) -> Dict[str, Any]:
    """Create a new Animation Blueprint asset.

    Args:
        name: Asset name (without path).
        skeleton_path: Full path to the target USkeleton.
        path: Package path (default: /Game/AnimBlueprints/).
        parent_class: Optional parent UAnimInstance subclass (e.g. "PRKAnimInstance").
        preview_mesh: Optional preview USkeletalMesh path.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"name": name, "skeleton_path": skeleton_path, "path": path}
        if parent_class:
            params["parent_class"] = parent_class
        if preview_mesh:
            params["preview_mesh"] = preview_mesh
        response = unreal.send_command("create_anim_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_anim_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_state_machine(
    blueprint_name: str,
    name: str = "New State Machine",
    pos_x: float = 200.0,
    pos_y: float = 0.0
) -> Dict[str, Any]:
    """Add a state machine node to the AnimGraph.

    Args:
        blueprint_name: AnimBlueprint name or full asset path.
        name: Display name of the state machine.
        pos_x: Graph X position.
        pos_y: Graph Y position.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_state_machine", {
            "blueprint_name": blueprint_name,
            "name": name,
            "pos_x": pos_x,
            "pos_y": pos_y
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_state_machine error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_state(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str,
    pos_x: float = 300.0,
    pos_y: float = 0.0
) -> Dict[str, Any]:
    """Add a state to a state machine.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: Name of the containing state machine.
        state_name: Unique name for the new state.
        pos_x: Graph X position inside the state machine.
        pos_y: Graph Y position inside the state machine.
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
            "pos_y": pos_y
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_transition(
    blueprint_name: str,
    state_machine_name: str,
    from_state: str,
    to_state: str,
    crossfade_duration: Optional[float] = None,
    priority: Optional[int] = None
) -> Dict[str, Any]:
    """Add a transition between two states.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: Name of the containing state machine.
        from_state: Source state name.
        to_state: Destination state name.
        crossfade_duration: Optional crossfade duration (seconds).
        priority: Optional priority order (lower = higher priority).
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
        }
        if crossfade_duration is not None:
            params["crossfade_duration"] = crossfade_duration
        if priority is not None:
            params["priority"] = priority
        response = unreal.send_command("add_transition", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_transition error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_default_state(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str
) -> Dict[str, Any]:
    """Wire a state machine's entry node to the specified state.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: Name of the state machine.
        state_name: State that should become the default (entry target).
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
        logger.error(f"set_default_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_state_animation(
    blueprint_name: str,
    state_machine_name: str,
    state_name: str,
    animation_path: str,
    loop: bool = True
) -> Dict[str, Any]:
    """Create a SequencePlayer inside a state and wire it to the state's Result node.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: Containing state machine name.
        state_name: Target state.
        animation_path: Full asset path to a UAnimSequenceBase.
        loop: Whether the sequence player should loop.
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
    automatic_rule_trigger_time: Optional[float] = None,
    trigger_time: Optional[float] = None,
    crossfade_duration: Optional[float] = None
) -> Dict[str, Any]:
    """Configure the rule on an existing transition.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: State machine containing the transition.
        from_state: Source state name.
        to_state: Destination state name.
        rule_type: "automatic" (default, rule based on sequence player in state)
                   or "time_remaining".
        automatic_rule_trigger_time: For rule_type="automatic": trigger time
                                     (negative = when the state's sequence ends).
        trigger_time: For rule_type="time_remaining": the trigger time threshold.
        crossfade_duration: Optional crossfade override (seconds).
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
        if automatic_rule_trigger_time is not None:
            params["automatic_rule_trigger_time"] = automatic_rule_trigger_time
        if trigger_time is not None:
            params["trigger_time"] = trigger_time
        if crossfade_duration is not None:
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
    """Create a BlendSpacePlayer inside a state and wire it to the state's Result node.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: Containing state machine.
        state_name: Target state.
        blend_space_path: Full asset path to a UBlendSpace.
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
def get_state_machine_info(
    blueprint_name: str,
    state_machine_name: str
) -> Dict[str, Any]:
    """Return the full structure of a state machine: states, transitions, inner nodes.

    Args:
        blueprint_name: AnimBlueprint name or path.
        state_machine_name: State machine to inspect.
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


# ============================================================
# Widget Blueprint Tools
# ============================================================

@mcp.tool()
def analyze_widget_blueprint(
    asset_path: str
) -> Dict[str, Any]:
    """Analyze a Widget Blueprint (UMG) and return its full hierarchy, bindings, and properties.

    Returns the widget tree structure including:
    - All child widgets with their types, names, and hierarchy
    - BindWidget/BindWidgetOptional property bindings
    - Slot properties (anchors, alignment, padding, size)
    - Visibility, render opacity, and interactability settings

    Args:
        asset_path: Content path to the Widget Blueprint (e.g., "/Game/PRK/UI/Widgets/Dialogue/DialogueWidget_WB")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("analyze_widget_blueprint", {
            "asset_path": asset_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"analyze_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_widget_details(
    asset_path: str,
    widget_name: str = ""
) -> Dict[str, Any]:
    """Get detailed properties of a specific widget within a Widget Blueprint.

    If widget_name is empty, returns details of the root widget.
    Otherwise, searches the hierarchy for the named widget.

    Returns detailed properties including:
    - Widget class and type
    - All UPROPERTY values (text, color, font, brush, etc.)
    - Layout properties (anchors, alignment, offsets)
    - Binding information (BindWidget connections)

    Args:
        asset_path: Content path to the Widget Blueprint
        widget_name: Name of the specific widget to inspect (empty = root)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path}
        if widget_name:
            params["widget_name"] = widget_name
        response = unreal.send_command("get_widget_details", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_widget_details error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_class_property(
    asset_path: str,
    widget_name: str,
    property_name: str,
    class_path: str = ""
) -> Dict[str, Any]:
    """Set a TSubclassOf<>/TSoftClassPtr<> UPROPERTY on a widget by name.

    Generic class-property setter. Covers UListViewBase/UTileView/UTreeView
    EntryWidgetClass, UCommonButtonBase Style, UCommonTextBlock Style, and any
    other class-typed widget UPROPERTY without a dedicated tool. Validates
    that the resolved class is a child of the property's required meta class
    before writing.

    class_path forms accepted:
      - WBP asset path: "/Game/PRK/UI/Widgets/Hud/WBP_PRKSquadRosterTile"
      - Explicit class path: "/Game/.../WBP_Foo.WBP_Foo_C"
      - C++ class name: "CommonButtonStyle" (with or without leading U)

    Pass empty class_path to clear the property.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget in the tree (e.g., "RosterListView")
        property_name: UPROPERTY name on that widget (e.g., "EntryWidgetClass")
        class_path: WBP path / class path / class name to assign; empty clears
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "property_name": property_name,
            "class_path": class_path,
        }
        response = unreal.send_command("set_widget_class_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_class_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_object_property(
    asset_path: str,
    widget_name: str,
    property_name: str,
    object_path: str = ""
) -> Dict[str, Any]:
    """Set a UObject reference UPROPERTY (TObjectPtr<>/TSoftObjectPtr<>) on a widget.

    Generic UObject-reference setter. Covers CommonUI Style refs and any other
    UObject-typed UPROPERTY:
      - UCommonButtonBase.Style → UCommonButtonStyle asset
      - UCommonTextBlock.Style → UCommonTextStyle asset
      - UCommonBorder.Style → UCommonBorderStyle asset
      - UCommonButtonBase.TriggeringInputAction → UInputAction asset

    Validates that the resolved object's class matches the property's expected
    class before writing. Pass empty object_path to clear the property.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget in the tree
        property_name: UPROPERTY name (e.g., "Style", "TriggeringInputAction")
        object_path: Asset path to the object to assign; empty clears
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "property_name": property_name,
            "object_path": object_path,
        }
        response = unreal.send_command("set_widget_object_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_object_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_common_style_asset(
    style_type: str,
    asset_name: str,
    destination_path: str = "/Game/UI/Styles/"
) -> Dict[str, Any]:
    """Create a CommonUI style data asset in the Content Browser.

    CommonUI style classes (UCommonButtonStyle / UCommonTextStyle /
    UCommonBorderStyle) have no default factory, so they cannot be created
    from the Content Browser right-click menu. This tool creates them
    programmatically and saves the asset.

    style_type accepts:
      - "button"  → UCommonButtonStyle
      - "text"    → UCommonTextStyle
      - "border"  → UCommonBorderStyle
      - any explicit class name that derives from one of the above

    Args:
        style_type: Style category ("button"/"text"/"border") or class name
        asset_name: Output asset name (e.g., "CBS_Primary")
        destination_path: Content folder (default "/Game/UI/Styles/")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "style_type": style_type,
            "asset_name": asset_name,
            "destination_path": destination_path,
        }
        response = unreal.send_command("create_common_style_asset", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_common_style_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_named_slot_content(
    asset_path: str,
    host_widget_name: str,
    slot_name: str,
    content_widget_name: str = ""
) -> Dict[str, Any]:
    """Fill a UNamedSlot exposed by an embedded UserWidget with existing tree content.

    Workflow:
      1. The host widget must be an embedded UUserWidget (or any
         INamedSlotInterface implementer like UNamedSlot itself) already in
         the outer WBP's tree.
      2. Construct the content widget via add_widget_child first — it must
         live in the outer WBP's WidgetTree.
      3. Call this tool to wire the content into the host's named slot.

    Pass empty content_widget_name to clear the slot.

    Args:
        asset_path: Path to the outer Widget Blueprint
        host_widget_name: The embedded user widget that exposes the slot
        slot_name: Named slot to fill (must match a slot the host exposes)
        content_widget_name: Existing widget in the outer tree; empty clears
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "host_widget_name": host_widget_name,
            "slot_name": slot_name,
            "content_widget_name": content_widget_name,
        }
        response = unreal.send_command("set_named_slot_content", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_named_slot_content error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def wrap_widget(
    asset_path: str,
    widget_name: str,
    wrapper_type: str,
    wrapper_name: str,
    padding_left: float = None,
    padding_top: float = None,
    padding_right: float = None,
    padding_bottom: float = None,
    h_align: str = "",
    v_align: str = "",
) -> Dict[str, Any]:
    """Wrap an existing widget in a new container, preserving its position in the parent.

    The wrapper takes the original widget's slot in the parent (slot props are
    copied when the parent uses a CanvasPanel — position/size/anchors stay
    correct). The original widget becomes the wrapper's only child. For
    HorizontalBox/VerticalBox parents only the slot index is preserved; set
    new wrapper-slot props via set_widget_slot_property if needed.

    Common use: wrap an existing widget in a UBorder for chrome, a USizeBox to
    constrain dimensions, or an UOverlay so additional siblings can stack.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Existing widget to wrap
        wrapper_type: New wrapper class — built-in name (Border, SizeBox,
            Overlay, ScaleBox, ...), C++ class, or WBP asset path. Must be a
            UPanelWidget subclass.
        wrapper_name: Name for the new wrapper widget (must not already exist)
        padding_left/top/right/bottom: Padding for the *target inside the wrapper*
            (only meaningful for wrapper slot types that accept padding).
        h_align/v_align: Alignment for the *target inside the wrapper*
            (Fill/Left/Center/Right and Fill/Top/Center/Bottom).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "wrapper_type": wrapper_type,
            "wrapper_name": wrapper_name,
        }
        if (padding_left is not None and padding_top is not None
                and padding_right is not None and padding_bottom is not None):
            params["padding"] = {
                "left": padding_left, "top": padding_top,
                "right": padding_right, "bottom": padding_bottom,
            }
        if h_align:
            params["h_align"] = h_align
        if v_align:
            params["v_align"] = v_align
        response = unreal.send_command("wrap_widget", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"wrap_widget error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_is_variable(
    asset_path: str,
    widget_name: str,
    is_variable: bool,
) -> Dict[str, Any]:
    """Toggle Widget->bIsVariable so the widget surfaces as a Blueprint variable.

    Widgets created via add_widget_child default to bIsVariable=false; without
    flipping this they cannot be referenced from the WBP's event graph. Call
    this on any MCP-authored widget that needs to be touched by Blueprint code.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Widget to modify
        is_variable: True to expose, False to hide from BP variables
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_widget_is_variable", {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "is_variable": is_variable,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_is_variable error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def replace_widget_class(
    asset_path: str,
    widget_name: str,
    new_widget_type: str,
) -> Dict[str, Any]:
    """Swap a widget's class in-place, preserving name, parent slot index, and
    (when both are panels) children.

    Slot props on the parent side are copied when the new slot type matches
    the old one (always true if the parent panel class is unchanged). If the
    new class isn't a panel, any existing children of the old widget are
    dropped — the response reports children_dropped > 0 in that case.

    Common use: migrate UButton → UCommonButtonBase, UTextBlock →
    UCommonTextBlock, UScrollBox → UCommonHierarchicalScrollBox.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Widget to replace
        new_widget_type: New class — built-in name, C++ class, or WBP asset path

    Returns:
        success, widget_name, new_class, children_moved, children_dropped,
        slot_props_preserved.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("replace_widget_class", {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "new_widget_type": new_widget_type,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"replace_widget_class error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_required_bindwidgets(asset_path: str) -> Dict[str, Any]:
    """List BindWidget / BindWidgetOptional properties on a WBP's parent C++ class.

    Walks the parent UClass for UWidget* UPROPERTYs marked with BindWidget,
    BindWidgetOptional, or OptionalWidget metadata, and reports whether the
    Widget Blueprint currently provides a same-named widget of a compatible
    class. Catches Layer_*-style binding failures before WBP compile.

    Returns per binding: name, expected_class, required, optional, provided,
    provided_class (when present), class_matches. Plus aggregate counts:
    required_count, optional_count, provided_count, missing_required_count,
    class_mismatch_count.

    Args:
        asset_path: Path to the Widget Blueprint (e.g., "/Game/PRK/UI/Widgets/WBP_PrimaryGameLayout")
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_required_bindwidgets", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_required_bindwidgets error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_widget_child(
    asset_path: str,
    widget_type: str,
    widget_name: str,
    parent_name: str = "",
    text: str = "",
    visibility: str = "Visible",
    position_x: float = 0,
    position_y: float = 0,
    size_x: float = 100,
    size_y: float = 50,
    anchor_min_x: float = 0,
    anchor_min_y: float = 0,
    anchor_max_x: float = -1,
    anchor_max_y: float = -1,
    alignment_x: float = 0,
    alignment_y: float = 0,
    auto_size: bool = False,
    z_order: int = 0
) -> Dict[str, Any]:
    """Add a child widget to a Widget Blueprint (UMG).

    Supports: TextBlock, Image, Button, CanvasPanel, VerticalBox, HorizontalBox,
    Overlay, Border, ProgressBar, Spacer, SizeBox, CheckBox, Slider, ScrollBox,
    WidgetSwitcher, RichTextBlock,
    CommonActivatableWidgetStack, CommonActivatableWidgetQueue,
    CommonActivatableWidgetSwitcher, CommonAnimatedSwitcher,
    CommonVisibilitySwitcher, CommonHierarchicalScrollBox,
    CommonBorder, CommonButtonBase, CommonTextBlock, CommonRichTextBlock,
    CommonLazyImage. Also accepts any C++ class name or WBP asset path.

    CommonUI containers extend UWidget (NOT UPanelWidget) — the WidgetTree
    treats them as leaves and runtime code pushes activatable widgets onto
    them. Add the container as a child of an existing panel; do not try to
    nest design-time children inside it.

    Args:
        asset_path: Path to the Widget Blueprint (e.g., "/Game/PRK/UI/Widgets/MyWidget")
        widget_type: Type of widget to add (e.g., "TextBlock", "Image", "Button")
        widget_name: Name for the new widget (must be unique in the tree)
        parent_name: Name of parent widget to add to (empty = root panel)
        text: Initial text for TextBlock widgets
        visibility: Visibility state (Visible, Collapsed, Hidden, HitTestInvisible, SelfHitTestInvisible)
        position_x: X position (for canvas panel children)
        position_y: Y position (for canvas panel children)
        size_x: Width (for canvas panel children)
        size_y: Height (for canvas panel children)
        anchor_min_x: Anchor minimum X (0-1)
        anchor_min_y: Anchor minimum Y (0-1)
        anchor_max_x: Anchor maximum X (-1 = same as min, for point anchor)
        anchor_max_y: Anchor maximum Y (-1 = same as min, for point anchor)
        alignment_x: Pivot X (0-1)
        alignment_y: Pivot Y (0-1)
        auto_size: Size to content
        z_order: Z-order for canvas panel children
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_type": widget_type,
            "widget_name": widget_name,
            "visibility": visibility,
        }
        if parent_name:
            params["parent_name"] = parent_name
        if text:
            params["text"] = text
        if position_x != 0 or position_y != 0:
            params["position"] = {"x": position_x, "y": position_y}
        if size_x != 100 or size_y != 50:
            params["size"] = {"x": size_x, "y": size_y}
        if anchor_min_x != 0 or anchor_min_y != 0 or anchor_max_x != -1 or anchor_max_y != -1:
            anchors = {"min_x": anchor_min_x, "min_y": anchor_min_y}
            if anchor_max_x >= 0:
                anchors["max_x"] = anchor_max_x
            if anchor_max_y >= 0:
                anchors["max_y"] = anchor_max_y
            params["anchors"] = anchors
        if alignment_x != 0 or alignment_y != 0:
            params["alignment"] = {"x": alignment_x, "y": alignment_y}
        if auto_size:
            params["auto_size"] = True
        if z_order != 0:
            params["z_order"] = z_order

        response = unreal.send_command("add_widget_child", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_widget_child error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_property(
    asset_path: str,
    widget_name: str,
    text: Any = None,
    visibility: str = "",
    render_opacity: float = None,
    is_enabled: bool = None,
    color_r: float = None,
    color_g: float = None,
    color_b: float = None,
    color_a: float = 1.0,
    texture: str = "",
    auto_wrap: bool = None,
    justification: str = "",
    draw_as: str = "",
    brush_size_x: float = None,
    brush_size_y: float = None,
    percent: float = None,
    fill_color_r: float = None,
    fill_color_g: float = None,
    fill_color_b: float = None,
    fill_color_a: float = 1.0,
    background_tint_r: float = None,
    background_tint_g: float = None,
    background_tint_b: float = None,
    background_tint_a: float = 1.0,
    bar_fill_type: str = "",
    bar_fill_style: str = "",
    is_marquee: bool = None,
    border_padding_x: float = None,
    border_padding_y: float = None,
    background_color_r: float = None,
    background_color_g: float = None,
    background_color_b: float = None,
    background_color_a: float = 1.0,
    background_texture: str = "",
    background_draw_as: str = "",
    padding_left: float = None,
    padding_top: float = None,
    padding_right: float = None,
    padding_bottom: float = None,
    h_align: str = "",
    v_align: str = "",
    content_color_r: float = None,
    content_color_g: float = None,
    content_color_b: float = None,
    content_color_a: float = 1.0,
    width_override: float = None,
    height_override: float = None,
    min_desired_width: float = None,
    min_desired_height: float = None,
    max_desired_width: float = None,
    max_desired_height: float = None,
    spacer_size_x: float = None,
    spacer_size_y: float = None,
    is_checked: bool = None,
    check_state: str = "",
    active_widget_index: int = None,
    font_size: float = None,
    shadow_color_r: float = None,
    shadow_color_g: float = None,
    shadow_color_b: float = None,
    shadow_color_a: float = 1.0,
    shadow_offset_x: float = None,
    shadow_offset_y: float = None,
    normal_color_r: float = None,
    normal_color_g: float = None,
    normal_color_b: float = None,
    normal_color_a: float = 1.0,
    hovered_color_r: float = None,
    hovered_color_g: float = None,
    hovered_color_b: float = None,
    hovered_color_a: float = 1.0,
    pressed_color_r: float = None,
    pressed_color_g: float = None,
    pressed_color_b: float = None,
    pressed_color_a: float = 1.0,
    disabled_color_r: float = None,
    disabled_color_g: float = None,
    disabled_color_b: float = None,
    disabled_color_a: float = 1.0,
    click_method: str = "",
    touch_method: str = "",
    press_method: str = "",
    value: float = None,
    min_value: float = None,
    max_value: float = None,
    step_size: float = None,
    locked: bool = None,
    indent_handle: bool = None,
    orientation: str = "",
    slider_handle_color_r: float = None,
    slider_handle_color_g: float = None,
    slider_handle_color_b: float = None,
    slider_handle_color_a: float = 1.0,
    slider_bar_color_r: float = None,
    slider_bar_color_g: float = None,
    slider_bar_color_b: float = None,
    slider_bar_color_a: float = 1.0,
    scroll_bar_visibility: str = "",
    scrollbar_thickness_x: float = None,
    scrollbar_thickness_y: float = None,
    scrollbar_padding_left: float = None,
    scrollbar_padding_top: float = None,
    scrollbar_padding_right: float = None,
    scrollbar_padding_bottom: float = None,
    always_show_scrollbar: bool = None,
    always_show_scrollbar_track: bool = None,
    allow_overscroll: bool = None,
    animate_wheel_scrolling: bool = None,
    wheel_scroll_multiplier: float = None,
    scroll_animation_interpolation_speed: float = None,
    scroll_offset: float = None,
    allow_right_click_drag_scrolling: bool = None,
    consume_mouse_wheel: str = "",
    scroll_when_focus_changes: str = "",
    navigation_destination: str = "",
    is_focusable: bool = None,
    consume_pointer_input: bool = None,
    is_touch_scrolling_enabled: bool = None,
    hint_text: str = None,
    error_text: str = None,
    is_password: bool = None,
    is_read_only: bool = None,
    is_caret_moved_when_gain_focus: bool = None,
    select_all_text_when_focused: bool = None,
    revert_text_on_escape: bool = None,
    clear_keyboard_focus_on_commit: bool = None,
    select_all_text_on_commit: bool = None,
    foreground_color_r: float = None,
    foreground_color_g: float = None,
    foreground_color_b: float = None,
    foreground_color_a: float = 1.0,
    combo_options: List[str] = None,
    selected_option: str = "",
    selected_index: int = None,
    content_padding_left: float = None,
    content_padding_top: float = None,
    content_padding_right: float = None,
    content_padding_bottom: float = None,
    max_list_height: float = None,
    has_down_arrow: bool = None,
    enable_gamepad_navigation_mode: bool = None,
    number_of_pieces: int = None,
    animate_horizontally: bool = None,
    animate_vertically: bool = None,
    animate_opacity: bool = None,
    period: float = None,
    radius: float = None,
    selection_mode: str = "",
    vertical_entry_spacing: float = None,
    horizontal_entry_spacing: float = None,
    scrollbar_visibility: str = "",
    scroll_into_view_alignment: str = "",
    is_pointer_scrolling_enabled: bool = None,
    is_gamepad_scrolling_enabled: bool = None,
    clear_list_items: bool = None,
    scroll_to_top: bool = None,
    scroll_to_bottom: bool = None,
    request_refresh: bool = None,
    regenerate_all_entries: bool = None,
    scroll_index_into_view: int = None,
    entry_height: float = None,
    entry_width: float = None,
    expand_all: bool = None,
    collapse_all: bool = None,
    background_margin_left: float = None,
    background_margin_top: float = None,
    background_margin_right: float = None,
    background_margin_bottom: float = None,
    fill_margin_left: float = None,
    fill_margin_top: float = None,
    fill_margin_right: float = None,
    fill_margin_bottom: float = None,
    margin_left: float = None,
    margin_top: float = None,
    margin_right: float = None,
    margin_bottom: float = None,
    inner_slot_padding_x: float = None,
    inner_slot_padding_y: float = None,
    wrap_size: float = None,
    explicit_wrap_size: bool = None,
    stretch: str = "",
    stretch_direction: str = "",
    user_specified_scale: float = None,
    ignore_inherited_scale: bool = None,
    delta: float = None,
    slider_exponent: float = None,
    min_fractional_digits: int = None,
    max_fractional_digits: int = None,
    min_slider_value: float = None,
    max_slider_value: float = None,
    always_uses_delta_snap: bool = None,
    enable_slider: bool = None,
    is_expanded: bool = None,
    is_expanded_animated: bool = None,
    max_height: float = None,
    header_padding_left: float = None,
    header_padding_top: float = None,
    header_padding_right: float = None,
    header_padding_bottom: float = None,
    area_padding_left: float = None,
    area_padding_top: float = None,
    area_padding_right: float = None,
    area_padding_bottom: float = None,
    border_color_r: float = None,
    border_color_g: float = None,
    border_color_b: float = None,
    border_color_a: float = 1.0,
) -> Dict[str, Any]:
    """Modify properties on an existing widget in a Widget Blueprint.

    Supports common properties on all widgets (visibility, opacity, enabled)
    plus type-specific properties for TextBlock, RichTextBlock, Image,
    ProgressBar, Border, SizeBox, Spacer, CheckBox, WidgetSwitcher, Button,
    Slider, ScrollBox, EditableText, EditableTextBox, MultiLineEditableText,
    MultiLineEditableTextBox, ComboBoxString, Throbber, and CircularThrobber.

    Pass only the fields you want to change; unspecified fields are left untouched.
    A no-op call (no fields passed) does not recompile.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget to modify
        text: Set text (TextBlock or RichTextBlock)
        visibility: Visible, Collapsed, Hidden, HitTestInvisible, SelfHitTestInvisible
        render_opacity: Render opacity (0.0 - 1.0)
        is_enabled: Enabled state (True/False)
        color_r/g/b/a: Generic color (TextBlock ColorAndOpacity, Image
            ColorAndOpacity, ProgressBar FillColorAndOpacity). For ProgressBar,
            ``fill_color_*`` is the explicit name and overrides ``color_*``.
        texture: Texture asset path (Image only)
        auto_wrap: Auto-wrap text (TextBlock only)
        justification: TextBlock text justification. ``Left``, ``Center``, or ``Right``.
        draw_as: Image brush draw type. One of: ``NoDrawType``, ``Box``,
            ``Border``, ``Image``, ``RoundedBox``.
        brush_size_x/y: Image brush size (SetBrushSize).
        percent: Progress 0-1 (ProgressBar only)
        fill_color_r/g/b/a: ProgressBar fill tint (SetFillColorAndOpacity).
        background_tint_r/g/b/a: ProgressBar background brush tint
            (mutates WidgetStyle.BackgroundImage.TintColor — there is no direct
            setter, the style is copied and reapplied).
        bar_fill_type: ProgressBar fill direction. One of: ``LeftToRight``,
            ``RightToLeft``, ``TopToBottom``, ``BottomToTop``,
            ``FillFromCenter``, ``FillFromCenterHorizontal``,
            ``FillFromCenterVertical``.
        bar_fill_style: ProgressBar fill rendering style. ``Mask`` or ``Scale``.
        is_marquee: ProgressBar marquee mode (ignores percent when True).
        border_padding_x/y: ProgressBar border padding vector.
        background_color_r/g/b/a: Border brush tint (SetBrushColor); also
            EditableTextBox / MultiLineEditableTextBox box background tint
            (WidgetStyle.BackgroundColor).
        background_texture: Border background texture asset path.
        background_draw_as: Border background brush draw type. Same enum as
            ``draw_as``. Mutates Background.DrawAs via copy/SetBrush — no direct
            setter exists for the enum.
        padding_left/top/right/bottom: Border content padding (FMargin).
        h_align: Border horizontal content alignment. ``Fill``, ``Left``,
            ``Center``, ``Right``.
        v_align: Border vertical content alignment. ``Fill``, ``Top``,
            ``Center``, ``Bottom``.
        content_color_r/g/b/a: Border content color and opacity tint.
        width_override / height_override: SizeBox per-axis size override.
        min_desired_width / min_desired_height: SizeBox minimum size.
        max_desired_width / max_desired_height: SizeBox maximum size.
        spacer_size_x/y: Spacer explicit size.
        is_checked: CheckBox checked state (bool convenience).
        check_state: CheckBox tri-state. ``Unchecked``, ``Checked``, ``Undetermined``.
        active_widget_index: WidgetSwitcher active page index.
        font_size: TextBlock / EditableText / MultiLineEditableText / EditableTextBox /
            MultiLineEditableTextBox font size (mutates FSlateFontInfo.Size via
            WidgetStyle copy/SetWidgetStyle — no direct setter).
        shadow_color_r/g/b/a: TextBlock shadow color and opacity.
        shadow_offset_x/y: TextBlock shadow offset.
        normal_color_r/g/b/a: Button Normal-state brush tint
            (FButtonStyle.Normal.TintColor — copy/SetStyle).
        hovered_color_r/g/b/a: Button Hovered-state brush tint.
        pressed_color_r/g/b/a: Button Pressed-state brush tint.
        disabled_color_r/g/b/a: Button Disabled-state brush tint.
        click_method: Button click method. ``DownAndUp``, ``MouseDown``,
            ``MouseUp``, ``PreciseClick``.
        touch_method: Button touch method. ``DownAndUp``, ``Down``,
            ``PreciseTap``.
        press_method: Button keyboard press method. ``DownAndUp``,
            ``ButtonPress``, ``ButtonRelease``.
        value: Slider current value.
        min_value/max_value: Slider value range.
        step_size: Slider step size.
        locked: Slider lock state (when True the slider does not respond to input).
        indent_handle: Slider indent handle visual mode.
        orientation: Slider / ScrollBox orientation. ``Horizontal`` or ``Vertical``.
        slider_handle_color_r/g/b/a: Slider handle color tint.
        slider_bar_color_r/g/b/a: Slider bar color tint.
        scroll_bar_visibility: ScrollBox scroll-bar visibility (uses the same
            enum as the widget ``visibility`` arg).
        scrollbar_thickness_x/y: ScrollBox scroll-bar thickness vector.
        scrollbar_padding_left/top/right/bottom: ScrollBox scroll-bar padding (FMargin).
        always_show_scrollbar: ScrollBox keeps the scroll bar visible always.
        always_show_scrollbar_track: ScrollBox keeps the scroll bar track visible always.
        allow_overscroll: ScrollBox allows overscroll past the end.
        animate_wheel_scrolling: ScrollBox animates wheel-driven scroll.
        wheel_scroll_multiplier: ScrollBox wheel scroll multiplier.
        scroll_animation_interpolation_speed: ScrollBox scroll animation speed.
        scroll_offset: ScrollBox absolute scroll offset.
        allow_right_click_drag_scrolling: ScrollBox enables right-click drag scrolling.
        consume_mouse_wheel: ScrollBox mouse-wheel consumption mode.
            ``WhenScrollingPossible``, ``Always``, ``Never``.
        scroll_when_focus_changes: ScrollBox auto-scroll on focus change.
            ``NoScroll``, ``InstantScroll``, ``AnimatedScroll``.
        navigation_destination: ScrollBox descendant scroll destination.
            ``IntoView``, ``TopOrLeft``, ``Center``, ``BottomOrRight``.
        is_focusable: ScrollBox focusable.
        consume_pointer_input: ScrollBox consumes pointer input.
        is_touch_scrolling_enabled: ScrollBox touch scrolling enabled.
        hint_text: EditableText / EditableTextBox / MultiLineEditableText(Box)
            hint/placeholder text.
        error_text: EditableTextBox / MultiLineEditableTextBox error text.
        is_password: EditableText / EditableTextBox password mode.
        is_read_only: EditableText / EditableTextBox / MultiLineEditableText(Box) read-only.
        is_caret_moved_when_gain_focus: EditableText caret behavior on focus.
        select_all_text_when_focused: EditableText / MultiLineEditableText select-all on focus.
        revert_text_on_escape: EditableText / MultiLineEditableText revert on Esc.
        clear_keyboard_focus_on_commit: EditableText / MultiLineEditableText commit behavior.
        select_all_text_on_commit: EditableText select all on commit.
        foreground_color_r/g/b/a: EditableTextBox / MultiLineEditableTextBox foreground tint.
        combo_options: ComboBoxString full option list (replaces existing — clears then re-adds).
        selected_option: ComboBoxString selected option string.
        selected_index: ComboBoxString selected option index.
        content_padding_left/top/right/bottom: ComboBoxString content padding.
        max_list_height: ComboBoxString dropdown max height.
        has_down_arrow: ComboBoxString shows the down-arrow indicator.
        enable_gamepad_navigation_mode: ComboBoxString gamepad navigation mode.
        number_of_pieces: Throbber / CircularThrobber piece count.
        animate_horizontally / animate_vertically / animate_opacity: Throbber
            animation toggles.
        period: CircularThrobber animation period.
        radius: CircularThrobber radius.
        selection_mode: ListView/TileView/TreeView selection mode.
            ``None``, ``Single``, ``SingleToggle``, ``Multi``.
        vertical_entry_spacing / horizontal_entry_spacing: ListView per-entry
            spacing (also used by TileView/TreeView).
        scrollbar_visibility: ListView scroll-bar visibility (uses the same
            ESlateVisibility enum as the widget ``visibility`` arg).
        scroll_into_view_alignment: ListView alignment when scrolling an item
            into view. ``IntoView``, ``TopOrLeft``, ``CenterAligned``, ``BottomOrRight``.
        is_pointer_scrolling_enabled / is_gamepad_scrolling_enabled: ListView
            scroll input toggles.
        clear_list_items: ListView action — pass True to ClearListItems().
        scroll_to_top / scroll_to_bottom: ListView actions — pass True to scroll.
        request_refresh / regenerate_all_entries: ListView actions — pass True.
        scroll_index_into_view: ListView action — index to scroll into view.
        entry_height / entry_width: TileView per-tile dimensions.
        expand_all / collapse_all: TreeView actions — pass True.
        background_margin_left/top/right/bottom: 9-slice margin on the
            background brush — Border (mutates Background.Margin via SetBrush)
            and ProgressBar (mutates WidgetStyle.BackgroundImage.Margin via
            SetWidgetStyle). Required when DrawAs is Box or Border, otherwise
            the texture stretches and corners distort on non-square widgets.
        fill_margin_left/top/right/bottom: ProgressBar 9-slice margin on
            WidgetStyle.FillImage.
        margin_left/top/right/bottom: Image brush 9-slice margin
            (mutates Brush.Margin via SetBrush).

        Note for ListView/TileView/TreeView: ``selected_index``,
        ``scroll_offset``, ``wheel_scroll_multiplier``, ``allow_overscroll``,
        and ``scrollbar_padding_*`` are shared with other branches above and
        dispatched by widget cast.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "widget_name": widget_name,
        }
        if text is not None:
            params["text"] = str(text)
        if visibility:
            params["visibility"] = visibility
        if render_opacity is not None:
            params["render_opacity"] = render_opacity
        if is_enabled is not None:
            params["is_enabled"] = is_enabled
        if color_r is not None and color_g is not None and color_b is not None:
            params["color"] = {"r": color_r, "g": color_g, "b": color_b, "a": color_a}
        if texture:
            params["texture"] = texture
        if auto_wrap is not None:
            params["auto_wrap"] = auto_wrap
        if justification:
            params["justification"] = justification
        if draw_as:
            params["draw_as"] = draw_as
        if brush_size_x is not None and brush_size_y is not None:
            params["brush_size"] = {"x": brush_size_x, "y": brush_size_y}
        if percent is not None:
            params["percent"] = percent
        if fill_color_r is not None and fill_color_g is not None and fill_color_b is not None:
            params["fill_color"] = {
                "r": fill_color_r, "g": fill_color_g, "b": fill_color_b, "a": fill_color_a,
            }
        if background_tint_r is not None and background_tint_g is not None and background_tint_b is not None:
            params["background_tint"] = {
                "r": background_tint_r, "g": background_tint_g, "b": background_tint_b, "a": background_tint_a,
            }
        if bar_fill_type:
            params["bar_fill_type"] = bar_fill_type
        if bar_fill_style:
            params["bar_fill_style"] = bar_fill_style
        if is_marquee is not None:
            params["is_marquee"] = is_marquee
        if border_padding_x is not None and border_padding_y is not None:
            params["border_padding"] = {"x": border_padding_x, "y": border_padding_y}
        if background_color_r is not None and background_color_g is not None and background_color_b is not None:
            params["background_color"] = {
                "r": background_color_r, "g": background_color_g, "b": background_color_b, "a": background_color_a,
            }
        if background_texture:
            params["background_texture"] = background_texture
        if background_draw_as:
            params["background_draw_as"] = background_draw_as
        if (padding_left is not None and padding_top is not None
                and padding_right is not None and padding_bottom is not None):
            params["padding"] = {
                "left": padding_left, "top": padding_top,
                "right": padding_right, "bottom": padding_bottom,
            }
        if h_align:
            params["h_align"] = h_align
        if v_align:
            params["v_align"] = v_align
        if content_color_r is not None and content_color_g is not None and content_color_b is not None:
            params["content_color"] = {
                "r": content_color_r, "g": content_color_g, "b": content_color_b, "a": content_color_a,
            }
        if width_override is not None:
            params["width_override"] = width_override
        if height_override is not None:
            params["height_override"] = height_override
        if min_desired_width is not None:
            params["min_desired_width"] = min_desired_width
        if min_desired_height is not None:
            params["min_desired_height"] = min_desired_height
        if max_desired_width is not None:
            params["max_desired_width"] = max_desired_width
        if max_desired_height is not None:
            params["max_desired_height"] = max_desired_height
        if spacer_size_x is not None and spacer_size_y is not None:
            params["spacer_size"] = {"x": spacer_size_x, "y": spacer_size_y}
        if is_checked is not None:
            params["is_checked"] = is_checked
        if check_state:
            params["check_state"] = check_state
        if active_widget_index is not None:
            params["active_widget_index"] = active_widget_index
        if font_size is not None:
            params["font_size"] = font_size
        if shadow_color_r is not None and shadow_color_g is not None and shadow_color_b is not None:
            params["shadow_color"] = {
                "r": shadow_color_r, "g": shadow_color_g, "b": shadow_color_b, "a": shadow_color_a,
            }
        if shadow_offset_x is not None and shadow_offset_y is not None:
            params["shadow_offset"] = {"x": shadow_offset_x, "y": shadow_offset_y}
        if normal_color_r is not None and normal_color_g is not None and normal_color_b is not None:
            params["normal_color"] = {
                "r": normal_color_r, "g": normal_color_g, "b": normal_color_b, "a": normal_color_a,
            }
        if hovered_color_r is not None and hovered_color_g is not None and hovered_color_b is not None:
            params["hovered_color"] = {
                "r": hovered_color_r, "g": hovered_color_g, "b": hovered_color_b, "a": hovered_color_a,
            }
        if pressed_color_r is not None and pressed_color_g is not None and pressed_color_b is not None:
            params["pressed_color"] = {
                "r": pressed_color_r, "g": pressed_color_g, "b": pressed_color_b, "a": pressed_color_a,
            }
        if disabled_color_r is not None and disabled_color_g is not None and disabled_color_b is not None:
            params["disabled_color"] = {
                "r": disabled_color_r, "g": disabled_color_g, "b": disabled_color_b, "a": disabled_color_a,
            }
        if click_method:
            params["click_method"] = click_method
        if touch_method:
            params["touch_method"] = touch_method
        if press_method:
            params["press_method"] = press_method
        if value is not None:
            params["value"] = value
        if min_value is not None:
            params["min_value"] = min_value
        if max_value is not None:
            params["max_value"] = max_value
        if step_size is not None:
            params["step_size"] = step_size
        if locked is not None:
            params["locked"] = locked
        if indent_handle is not None:
            params["indent_handle"] = indent_handle
        if orientation:
            params["orientation"] = orientation
        if slider_handle_color_r is not None and slider_handle_color_g is not None and slider_handle_color_b is not None:
            params["slider_handle_color"] = {
                "r": slider_handle_color_r, "g": slider_handle_color_g,
                "b": slider_handle_color_b, "a": slider_handle_color_a,
            }
        if slider_bar_color_r is not None and slider_bar_color_g is not None and slider_bar_color_b is not None:
            params["slider_bar_color"] = {
                "r": slider_bar_color_r, "g": slider_bar_color_g,
                "b": slider_bar_color_b, "a": slider_bar_color_a,
            }
        if scroll_bar_visibility:
            params["scroll_bar_visibility"] = scroll_bar_visibility
        if scrollbar_thickness_x is not None and scrollbar_thickness_y is not None:
            params["scrollbar_thickness"] = {"x": scrollbar_thickness_x, "y": scrollbar_thickness_y}
        if (scrollbar_padding_left is not None and scrollbar_padding_top is not None
                and scrollbar_padding_right is not None and scrollbar_padding_bottom is not None):
            params["scrollbar_padding"] = {
                "left": scrollbar_padding_left, "top": scrollbar_padding_top,
                "right": scrollbar_padding_right, "bottom": scrollbar_padding_bottom,
            }
        if always_show_scrollbar is not None:
            params["always_show_scrollbar"] = always_show_scrollbar
        if always_show_scrollbar_track is not None:
            params["always_show_scrollbar_track"] = always_show_scrollbar_track
        if allow_overscroll is not None:
            params["allow_overscroll"] = allow_overscroll
        if animate_wheel_scrolling is not None:
            params["animate_wheel_scrolling"] = animate_wheel_scrolling
        if wheel_scroll_multiplier is not None:
            params["wheel_scroll_multiplier"] = wheel_scroll_multiplier
        if scroll_animation_interpolation_speed is not None:
            params["scroll_animation_interpolation_speed"] = scroll_animation_interpolation_speed
        if scroll_offset is not None:
            params["scroll_offset"] = scroll_offset
        if allow_right_click_drag_scrolling is not None:
            params["allow_right_click_drag_scrolling"] = allow_right_click_drag_scrolling
        if consume_mouse_wheel:
            params["consume_mouse_wheel"] = consume_mouse_wheel
        if scroll_when_focus_changes:
            params["scroll_when_focus_changes"] = scroll_when_focus_changes
        if navigation_destination:
            params["navigation_destination"] = navigation_destination
        if is_focusable is not None:
            params["is_focusable"] = is_focusable
        if consume_pointer_input is not None:
            params["consume_pointer_input"] = consume_pointer_input
        if is_touch_scrolling_enabled is not None:
            params["is_touch_scrolling_enabled"] = is_touch_scrolling_enabled
        if hint_text is not None:
            params["hint_text"] = str(hint_text)
        if error_text is not None:
            params["error_text"] = str(error_text)
        if is_password is not None:
            params["is_password"] = is_password
        if is_read_only is not None:
            params["is_read_only"] = is_read_only
        if is_caret_moved_when_gain_focus is not None:
            params["is_caret_moved_when_gain_focus"] = is_caret_moved_when_gain_focus
        if select_all_text_when_focused is not None:
            params["select_all_text_when_focused"] = select_all_text_when_focused
        if revert_text_on_escape is not None:
            params["revert_text_on_escape"] = revert_text_on_escape
        if clear_keyboard_focus_on_commit is not None:
            params["clear_keyboard_focus_on_commit"] = clear_keyboard_focus_on_commit
        if select_all_text_on_commit is not None:
            params["select_all_text_on_commit"] = select_all_text_on_commit
        if foreground_color_r is not None and foreground_color_g is not None and foreground_color_b is not None:
            params["foreground_color"] = {
                "r": foreground_color_r, "g": foreground_color_g,
                "b": foreground_color_b, "a": foreground_color_a,
            }
        if combo_options is not None:
            params["combo_options"] = list(combo_options)
        if selected_option:
            params["selected_option"] = selected_option
        if selected_index is not None:
            params["selected_index"] = selected_index
        if (content_padding_left is not None and content_padding_top is not None
                and content_padding_right is not None and content_padding_bottom is not None):
            params["content_padding"] = {
                "left": content_padding_left, "top": content_padding_top,
                "right": content_padding_right, "bottom": content_padding_bottom,
            }
        if max_list_height is not None:
            params["max_list_height"] = max_list_height
        if has_down_arrow is not None:
            params["has_down_arrow"] = has_down_arrow
        if enable_gamepad_navigation_mode is not None:
            params["enable_gamepad_navigation_mode"] = enable_gamepad_navigation_mode
        if number_of_pieces is not None:
            params["number_of_pieces"] = number_of_pieces
        if animate_horizontally is not None:
            params["animate_horizontally"] = animate_horizontally
        if animate_vertically is not None:
            params["animate_vertically"] = animate_vertically
        if animate_opacity is not None:
            params["animate_opacity"] = animate_opacity
        if period is not None:
            params["period"] = period
        if radius is not None:
            params["radius"] = radius
        if selection_mode:
            params["selection_mode"] = selection_mode
        if vertical_entry_spacing is not None:
            params["vertical_entry_spacing"] = vertical_entry_spacing
        if horizontal_entry_spacing is not None:
            params["horizontal_entry_spacing"] = horizontal_entry_spacing
        if scrollbar_visibility:
            params["scrollbar_visibility"] = scrollbar_visibility
        if scroll_into_view_alignment:
            params["scroll_into_view_alignment"] = scroll_into_view_alignment
        if is_pointer_scrolling_enabled is not None:
            params["is_pointer_scrolling_enabled"] = is_pointer_scrolling_enabled
        if is_gamepad_scrolling_enabled is not None:
            params["is_gamepad_scrolling_enabled"] = is_gamepad_scrolling_enabled
        if clear_list_items is not None:
            params["clear_list_items"] = clear_list_items
        if scroll_to_top is not None:
            params["scroll_to_top"] = scroll_to_top
        if scroll_to_bottom is not None:
            params["scroll_to_bottom"] = scroll_to_bottom
        if request_refresh is not None:
            params["request_refresh"] = request_refresh
        if regenerate_all_entries is not None:
            params["regenerate_all_entries"] = regenerate_all_entries
        if scroll_index_into_view is not None:
            params["scroll_index_into_view"] = scroll_index_into_view
        if entry_height is not None:
            params["entry_height"] = entry_height
        if entry_width is not None:
            params["entry_width"] = entry_width
        if expand_all is not None:
            params["expand_all"] = expand_all
        if collapse_all is not None:
            params["collapse_all"] = collapse_all
        if (background_margin_left is not None and background_margin_top is not None
                and background_margin_right is not None and background_margin_bottom is not None):
            params["background_margin"] = {
                "left": background_margin_left, "top": background_margin_top,
                "right": background_margin_right, "bottom": background_margin_bottom,
            }
        if (fill_margin_left is not None and fill_margin_top is not None
                and fill_margin_right is not None and fill_margin_bottom is not None):
            params["fill_margin"] = {
                "left": fill_margin_left, "top": fill_margin_top,
                "right": fill_margin_right, "bottom": fill_margin_bottom,
            }
        if (margin_left is not None and margin_top is not None
                and margin_right is not None and margin_bottom is not None):
            params["margin"] = {
                "left": margin_left, "top": margin_top,
                "right": margin_right, "bottom": margin_bottom,
            }
        if inner_slot_padding_x is not None and inner_slot_padding_y is not None:
            params["inner_slot_padding"] = {"x": inner_slot_padding_x, "y": inner_slot_padding_y}
        if wrap_size is not None:
            params["wrap_size"] = wrap_size
        if explicit_wrap_size is not None:
            params["explicit_wrap_size"] = explicit_wrap_size
        if stretch:
            params["stretch"] = stretch
        if stretch_direction:
            params["stretch_direction"] = stretch_direction
        if user_specified_scale is not None:
            params["user_specified_scale"] = user_specified_scale
        if ignore_inherited_scale is not None:
            params["ignore_inherited_scale"] = ignore_inherited_scale
        if delta is not None:
            params["delta"] = delta
        if slider_exponent is not None:
            params["slider_exponent"] = slider_exponent
        if min_fractional_digits is not None:
            params["min_fractional_digits"] = min_fractional_digits
        if max_fractional_digits is not None:
            params["max_fractional_digits"] = max_fractional_digits
        if min_slider_value is not None:
            params["min_slider_value"] = min_slider_value
        if max_slider_value is not None:
            params["max_slider_value"] = max_slider_value
        if always_uses_delta_snap is not None:
            params["always_uses_delta_snap"] = always_uses_delta_snap
        if enable_slider is not None:
            params["enable_slider"] = enable_slider
        if is_expanded is not None:
            params["is_expanded"] = is_expanded
        if is_expanded_animated is not None:
            params["is_expanded_animated"] = is_expanded_animated
        if max_height is not None:
            params["max_height"] = max_height
        if (header_padding_left is not None and header_padding_top is not None
                and header_padding_right is not None and header_padding_bottom is not None):
            params["header_padding"] = {
                "left": header_padding_left, "top": header_padding_top,
                "right": header_padding_right, "bottom": header_padding_bottom,
            }
        if (area_padding_left is not None and area_padding_top is not None
                and area_padding_right is not None and area_padding_bottom is not None):
            params["area_padding"] = {
                "left": area_padding_left, "top": area_padding_top,
                "right": area_padding_right, "bottom": area_padding_bottom,
            }
        if border_color_r is not None and border_color_g is not None and border_color_b is not None:
            params["border_color"] = {
                "r": border_color_r, "g": border_color_g, "b": border_color_b, "a": border_color_a,
            }

        response = unreal.send_command("set_widget_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_widget(
    asset_path: str,
    widget_name: str
) -> Dict[str, Any]:
    """Remove a widget from a Widget Blueprint.

    Removes the named widget and all its children from the widget tree.
    The blueprint is recompiled after removal.

    Args:
        asset_path: Path to the Widget Blueprint
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
def create_widget_blueprint(
    name: str,
    path: str = "/Game/Blueprints/Widgets",
    parent_class: str = ""
) -> Dict[str, Any]:
    """Create a new Widget Blueprint (UMG).

    Creates an empty UWidgetBlueprint that can be opened in the UMG Designer.
    Optionally specify a C++ parent class (must extend UUserWidget).

    Args:
        name: Name for the new widget blueprint (e.g., "WBP_BailPanel")
        path: Content Browser folder path (e.g., "/Game/PRK/UI/Widgets")
        parent_class: Optional C++ parent class name (e.g., "PRKDialogueWidget"). Defaults to UUserWidget.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"name": name, "path": path}
        if parent_class:
            params["parent_class"] = parent_class
        response = unreal.send_command("create_widget_blueprint", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def save_widget_blueprint(
    asset_path: str,
    only_if_dirty: bool = True
) -> Dict[str, Any]:
    """Persist a Widget Blueprint asset to disk.

    Args:
        asset_path: Path to the Widget Blueprint (e.g., "/Game/PRK/UI/Widgets/WBP_Foo")
        only_if_dirty: Only save if the asset has unsaved changes (default True)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("save_widget_blueprint", {
            "asset_path": asset_path,
            "only_if_dirty": only_if_dirty
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"save_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_widget_blueprint(
    asset_path: str
) -> Dict[str, Any]:
    """Delete a Widget Blueprint asset.

    Refuses to delete if the asset is not a UWidgetBlueprint.

    Args:
        asset_path: Path to the Widget Blueprint to delete
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_widget_blueprint", {
            "asset_path": asset_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_widget_blueprint(
    source_path: str,
    destination_path: str
) -> Dict[str, Any]:
    """Rename or move a Widget Blueprint asset.

    Args:
        source_path: Existing Widget Blueprint path
        destination_path: New full path including new asset name
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("rename_widget_blueprint", {
            "source_path": source_path,
            "destination_path": destination_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"rename_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def duplicate_widget_blueprint(
    source_path: str,
    destination_path: str
) -> Dict[str, Any]:
    """Duplicate a Widget Blueprint asset.

    Args:
        source_path: Existing Widget Blueprint path
        destination_path: Full path for the new copy
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("duplicate_widget_blueprint", {
            "source_path": source_path,
            "destination_path": destination_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"duplicate_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_widget_blueprints(
    directory: str = "/Game/",
    limit: int = 200,
    recursive: bool = True
) -> Dict[str, Any]:
    """List Widget Blueprints in a content directory.

    Args:
        directory: Content path to search (default "/Game/")
        limit: Maximum results to return
        recursive: Recurse into subdirectories
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_widget_blueprints", {
            "directory": directory,
            "limit": limit,
            "recursive": recursive
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_widget_blueprints error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def move_widget(
    asset_path: str,
    widget_name: str,
    new_parent_name: str
) -> Dict[str, Any]:
    """Reparent a widget under a different panel within the same Widget Blueprint.

    The widget retains its children. Cycles (reparenting under a descendant) are rejected.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget to move
        new_parent_name: Name of the destination panel widget
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("move_widget", {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "new_parent_name": new_parent_name
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"move_widget error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reorder_widget_children(
    asset_path: str,
    widget_name: str,
    new_index: int
) -> Dict[str, Any]:
    """Move a widget to a new index within its parent panel's child list.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget to reorder
        new_index: Zero-based target index in the parent's child list (clamped to range)
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("reorder_widget_children", {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "new_index": new_index
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reorder_widget_children error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_slot_property(
    asset_path: str,
    widget_name: str,
    # Canvas slot
    position_x: float = None,
    position_y: float = None,
    size_x: float = None,
    size_y: float = None,
    anchor_min_x: float = None,
    anchor_min_y: float = None,
    anchor_max_x: float = None,
    anchor_max_y: float = None,
    alignment_x: float = None,
    alignment_y: float = None,
    auto_size: bool = None,
    z_order: int = None,
    # Box / Overlay slot
    h_align: str = "",
    v_align: str = "",
    padding_left: float = None,
    padding_top: float = None,
    padding_right: float = None,
    padding_bottom: float = None,
    size_fill: float = None,
    size_auto: bool = None,
    # SizeBox widget (commonly fixed up alongside slot props)
    width_override: float = None,
    height_override: float = None,
    min_desired_width: float = None,
    min_desired_height: float = None,
    max_desired_width: float = None,
    max_desired_height: float = None,
    max_aspect_ratio: float = None,
    # Grid / UniformGrid slot
    row: int = None,
    column: int = None,
    row_span: int = None,
    column_span: int = None,
    layer: int = None,
    nudge_x: float = None,
    nudge_y: float = None,
    # WrapBox slot
    fill_empty_space: bool = None,
    fill_span_when_less_than: float = None,
    force_new_line: bool = None,
) -> Dict[str, Any]:
    """Set slot properties on an existing widget (and SizeBox-specific size props).

    Slot properties are routed by the slot type:
    - CanvasPanelSlot: position, size, anchors, alignment, auto_size, z_order
    - HorizontalBoxSlot / VerticalBoxSlot / StackBoxSlot: padding, h_align,
      v_align, size_fill or size_auto
    - ScrollBoxSlot: padding, h_align, v_align, size_fill, size_auto
    - OverlaySlot / BorderSlot / SizeBoxSlot / ScaleBoxSlot: padding, h_align, v_align
    - GridSlot: row, column, row_span, column_span, layer, nudge_x/y, padding,
      h_align, v_align
    - UniformGridSlot: row, column, h_align, v_align
    - WrapBoxSlot: padding, h_align, v_align, fill_empty_space,
      fill_span_when_less_than, force_new_line

    For USizeBox widgets, width_override / height_override / min_desired_* / max_desired_* /
    max_aspect_ratio are applied to the widget itself (not the slot).

    Pass only the fields you want to set; unspecified fields are left untouched.

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget whose slot to modify
        h_align: Horizontal alignment for box/overlay slots ("Fill", "Left", "Center", "Right")
        v_align: Vertical alignment for box/overlay slots ("Fill", "Top", "Center", "Bottom")
        size_fill: Set box slot size to Fill with this value
        size_auto: Set box slot size to Automatic
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "widget_name": widget_name,
        }

        if position_x is not None and position_y is not None:
            params["position"] = {"x": position_x, "y": position_y}
        if size_x is not None and size_y is not None:
            params["size"] = {"x": size_x, "y": size_y}
        if anchor_min_x is not None and anchor_min_y is not None:
            anchors: Dict[str, float] = {"min_x": anchor_min_x, "min_y": anchor_min_y}
            if anchor_max_x is not None:
                anchors["max_x"] = anchor_max_x
            if anchor_max_y is not None:
                anchors["max_y"] = anchor_max_y
            params["anchors"] = anchors
        if alignment_x is not None and alignment_y is not None:
            params["alignment"] = {"x": alignment_x, "y": alignment_y}
        if auto_size is not None:
            params["auto_size"] = auto_size
        if z_order is not None:
            params["z_order"] = z_order

        if h_align:
            params["h_align"] = h_align
        if v_align:
            params["v_align"] = v_align
        if any(p is not None for p in (padding_left, padding_top, padding_right, padding_bottom)):
            padding: Dict[str, float] = {}
            if padding_left is not None: padding["left"] = padding_left
            if padding_top is not None: padding["top"] = padding_top
            if padding_right is not None: padding["right"] = padding_right
            if padding_bottom is not None: padding["bottom"] = padding_bottom
            params["padding"] = padding
        if size_fill is not None:
            params["size_fill"] = size_fill
        if size_auto is not None:
            params["size_auto"] = size_auto

        if width_override is not None: params["width_override"] = width_override
        if height_override is not None: params["height_override"] = height_override
        if min_desired_width is not None: params["min_desired_width"] = min_desired_width
        if min_desired_height is not None: params["min_desired_height"] = min_desired_height
        if max_desired_width is not None: params["max_desired_width"] = max_desired_width
        if max_desired_height is not None: params["max_desired_height"] = max_desired_height
        if max_aspect_ratio is not None: params["max_aspect_ratio"] = max_aspect_ratio

        # Grid / UniformGrid slot.
        if row is not None: params["row"] = row
        if column is not None: params["column"] = column
        if row_span is not None: params["row_span"] = row_span
        if column_span is not None: params["column_span"] = column_span
        if layer is not None: params["layer"] = layer
        if nudge_x is not None and nudge_y is not None:
            params["nudge"] = {"x": nudge_x, "y": nudge_y}

        # WrapBox slot.
        if fill_empty_space is not None: params["fill_empty_space"] = fill_empty_space
        if fill_span_when_less_than is not None: params["fill_span_when_less_than"] = fill_span_when_less_than
        if force_new_line is not None: params["force_new_line"] = force_new_line

        response = unreal.send_command("set_widget_slot_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_slot_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_widget_root(
    asset_path: str,
    widget_name: str,
    reparent_old_root: bool = False
) -> Dict[str, Any]:
    """Replace the WidgetTree's root with an existing widget in the same blueprint.

    The target widget is detached from its current parent (if any) and becomes
    the root. If reparent_old_root is True and the new root is a panel widget,
    the previous root is added as a child of the new root; otherwise it is
    orphaned (and will be GC'd at next save).

    Args:
        asset_path: Path to the Widget Blueprint
        widget_name: Name of the widget to promote to root
        reparent_old_root: If True and new root is a panel, add the old root as a child
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_widget_root", {
            "asset_path": asset_path,
            "widget_name": widget_name,
            "reparent_old_root": reparent_old_root
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_widget_root error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def compile_widget_blueprint(
    asset_path: str
) -> Dict[str, Any]:
    """Recompile a Widget Blueprint without making other edits.

    Useful to verify the blueprint compiles cleanly after a sequence of edits,
    or to silently clear an orphaned WidgetVariableNameToGuidMap entry left by
    prior tooling. Always runs the variable-GUID reconcile pass before compiling.

    Args:
        asset_path: Path to the Widget Blueprint
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("compile_widget_blueprint", {
            "asset_path": asset_path
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"compile_widget_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_data_asset(
    data_asset_class: str,
    name: str,
    package_path: str = "/Game/Data/",
    properties: Dict[str, Any] = {},
    save: bool = False
) -> Dict[str, Any]:
    """Create a new Data Asset instance of the specified class.

    Creates a UObject-derived asset in the Content Browser. Supports any
    concrete C++ class that extends UObject — UDataAsset / UPrimaryDataAsset
    subclasses (UPRKMoveset, UPRKItemData, UPRKWeaponData, ...) AND plain
    UObject-derived data containers (e.g., third-party species/biome classes
    that extend UObject directly). Abstract/deprecated classes and
    AActor/UActorComponent subclasses are rejected — Actors/Components have
    different creation paths and don't round-trip as Content Browser assets.

    Args:
        data_asset_class: Class to instantiate. Accepts short name ("PRKMoveset"),
            short name without U-prefix ("Moveset"), or full script path
            ("/Script/PRK.PRKMoveset"). Resolved across every loaded module.
        name: Asset name (e.g., "DA_Moveset_Sword", "DA_Item_IronOre").
        package_path: Content Browser folder path (default: "/Game/Data/").
        properties: Optional dict of UPROPERTY names to initial values.
            Supports: bool, int, float, string, enum, FName, FText,
            soft/hard object refs (TSoftObjectPtr — string asset path),
            class refs (TSubclassOf — string class name or /Script/... path),
            soft class refs (TSoftClassPtr — same),
            FGameplayTag (tag string), FGameplayTagContainer (list of strings),
            structs (nested dict), arrays (list), sets (list),
            maps (JSON object for string/name/enum keys, or list of
            {"key": ..., "value": ...} entries for arbitrary keys).
            Dot/bracket paths ("Struct.Field", "Array[2].Field") are supported.
            Each property edit fires PreEditChange / PostEditChangeProperty.
            Unsettable types will be reported as warnings.
        save: If true, save the asset to disk after creation. Otherwise the
            asset lives only in memory until the editor is saved.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "data_asset_class": data_asset_class,
            "name": name,
            "package_path": package_path,
        }
        if properties:
            params["properties"] = properties
        if save:
            params["save"] = True
        response = unreal.send_command("create_data_asset", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_data_asset(
    asset_path: str,
    properties: Dict[str, Any],
    save: bool = True
) -> Dict[str, Any]:
    """Update properties on an existing Data Asset.

    Loads an existing DataAsset and sets the specified properties. Each edit
    is wrapped in PreEditChange / PostEditChangeProperty so derived state on
    the asset stays consistent.

    TMap MERGE SEMANTICS (changed 2026-05-26):
    Map property writes default to UPSERT (merge). Keys present in the JSON
    object replace specific entries; keys NOT in the JSON are preserved.
    Previously this dropped all unmentioned entries — see PRK feedback memory
    for the old footgun. To opt back into the old wipe-and-replace behavior
    for a specific map, include the sentinel "__replace": true INSIDE the
    map's JSON object:
        properties={"MyMap": {"__replace": True, "KeyA": 1, "KeyB": 2}}

    Args:
        asset_path: Full path to the existing DataAsset
            (e.g., "/Game/PRK/Data/Combat/DA_PRKBloodVFXProfile").
        properties: Dict of UPROPERTY names to values. Supported types:
            bool, int, float, string, enum (name or number), FName, FText,
            TArray (list), TSet (list), TMap (object or list of {key,value}),
            TSoftObjectPtr (asset path), TSubclassOf (class name or path),
            TSoftClassPtr (class name or path), hard UObject refs (asset path),
            FGameplayTag (tag string), FGameplayTagContainer (list of strings),
            nested structs (dict with field names).
            Dot/bracket paths are supported: "Details.Range", "Attacks[0].Tag".
        save: If true (default), save the asset to disk after updating. Pass
            false only if you intend to batch additional edits before saving —
            otherwise unsaved soft-pointer / instanced-subobject edits can be
            dropped on package GC and a subsequent read will return the disk state.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "properties": properties,
            "save": save,
        }
        response = unreal.send_command("update_data_asset", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_dataasset_property(
    asset_path: str,
    property_path: str,
    value: Any,
    save: bool = False
) -> Dict[str, Any]:
    """Set ONE property on a loaded asset and READ IT BACK for verification, in one call.

    The focused, verified alternative to update_data_asset for a single field. Uses the same
    write machinery (handles TSoftClassPtr / TSoftObjectPtr / TSubclassOf / hard object refs /
    array-element and nested-struct paths), then re-reads the exact path and returns
    `stored_value` — so you confirm intent-vs-persisted in the same call instead of a second
    read. Kills the soft-ref "write looked fine but silently dropped" trap.

    Works on any loaded UObject asset (DataAsset, material instance, etc.), not just UDataAsset.

    Args:
        asset_path: Full asset path (e.g. "/Game/PRK/.../DA_PlaceableActors").
        property_path: Single property, dotted/indexed allowed — "ActorClass",
            "Entries[3].ActorClass", "Details.Range".
        value: The value, in the same JSON forms update_data_asset accepts (asset/class path
            string for soft/hard refs, number, bool, list, nested dict, tag string, ...).
        save: Save to disk after writing. Default False — set True for soft-pointer / instanced
            edits you won't immediately batch further, else a later read may return disk state.

    Returns: stored_value (read-back of property_path), value_type, optional warning (partial
    success, e.g. one array element's soft-ref failed), saved.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "property_path": property_path,
            "value": value,
            "save": save,
        }
        response = unreal.send_command("set_dataasset_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_dataasset_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_dataasset_property(
    asset_path: str,
    property_path: str
) -> Dict[str, Any]:
    """Read ONE property from a loaded asset by path — including deep/indexed paths.

    read_data_asset dumps every property; this targets a single path
    (e.g. "FamilyRules[2].SoftTarget") and returns just its value, value_type, and
    property_class. The read half of the set_dataasset_property verify loop.

    Works on any loaded UObject asset, not just UDataAsset.

    Args:
        asset_path: Full asset path.
        property_path: Single property, dotted/indexed allowed.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {"asset_path": asset_path, "property_path": property_path}
        response = unreal.send_command("get_dataasset_property", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_dataasset_property error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def data_asset_array_remove(
    asset_path: str,
    property_path: str,
    index: int,
    count: int = 1,
    save: bool = True,
) -> Dict[str, Any]:
    """Remove element(s) from a TArray property IN PLACE — no whole-array rewrite.

    update_data_asset's only verb for a TArray is whole-container replace: it
    empties the array and rebuilds it from the JSON you pass, round-tripping
    EVERY element through JSON. For arrays of complex structs an imperfect
    round-trip can silently corrupt the elements you meant to keep, and a
    hand-rebuilt array risks reordering — dangerous when array order is itself
    a semantic index (e.g. a layered material's surface_types, where position
    == surface index).

    This removes only the targeted slice via FScriptArrayHelper::RemoveValues.
    The kept elements stay as native bytes (never serialized), so order is
    preserved exactly and nothing else is touched. Fires PreEdit/
    PostEditChangeProperty on the top-level property so derived asset state
    rebuilds. (Note: for simple object/primitive arrays the whole-array rewrite
    is already safe — reach for this when the element type is a complex struct
    or when array order is load-bearing.)

    Args:
        asset_path: Full path to the DataAsset (or any UObject asset).
        property_path: Path to the ARRAY property itself — NOT an element.
            Top-level ("SurfaceTypes") or nested via dot/bracket through structs
            and struct-arrays ("Config.Layers", "Groups[0].Entries"). Does not
            descend into TMaps.
        index: Zero-based index of the first element to remove.
        count: Number of contiguous elements to remove (default 1). Clamped to
            the array tail.
        save: Save the asset after removal (default True).

    Returns:
        {success, removed_count, old_size, new_size, saved}.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "property_path": property_path,
            "index": index,
            "count": count,
            "save": save,
        }
        response = unreal.send_command("data_asset_array_remove", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"data_asset_array_remove error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_data_asset(asset_path: str) -> Dict[str, Any]:
    """Read all UPROPERTY values from an existing Data Asset.

    Returns the class, class hierarchy, and a full JSON serialization of
    every non-deprecated UPROPERTY on the asset. Use this before
    update_data_asset to discover current values.

    Round-trip shapes:
        - External UObject ref     → "/Game/.../Asset.Asset" (asset path)
        - Instanced subobject      → {"_class": "...", "properties": {...}}
          The same {class, properties} envelope update_data_asset accepts
          for writes — read, edit a field, write back unchanged.
        - Instanced TArray/TMap    → array/object of envelopes above.

    To append one element to an Instanced TArray (e.g., AthenaPreset.Tasks),
    read the asset, append the new {"class": "...", "properties": {...}} to
    the array, and write the full array back via update_data_asset — partial
    array writes are not supported (TArray writes replace the array).

    Args:
        asset_path: Full path to the DataAsset
            (e.g., "/Game/PRK/Data/Combat/DA_PRKBloodVFXProfile").
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
def list_data_assets(
    directory: str = "/Game/",
    limit: int = 100,
    class_filter: Optional[str] = None
) -> Dict[str, Any]:
    """List DataAsset instances in a Content Browser directory.

    Args:
        directory: Folder to search, recursive (default: "/Game/").
        limit: Maximum number of results to return.
        class_filter: Optional class to filter by. Accepts short name
            ("PRKMoveset") or full script path ("/Script/PRK.PRKMoveset").
            When omitted, lists every UDataAsset subclass.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"directory": directory, "limit": limit}
        if class_filter:
            params["class_filter"] = class_filter
        response = unreal.send_command("list_data_assets", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_data_assets error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_curve_tables(
    directory: str = "/Game/",
    limit: int = 100
) -> Dict[str, Any]:
    """List UCurveTable assets under a Content Browser directory.

    Args:
        directory: Folder to search, recursive (default: "/Game/").
        limit: Maximum number of results to return.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_curve_tables", {
            "directory": directory,
            "limit": limit,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_curve_tables error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_curve_table(asset_path: str) -> Dict[str, Any]:
    """Read every row of a UCurveTable asset.

    Returns the table's curve_mode ("RichCurves", "SimpleCurves", or "Empty")
    and a list of rows. Each row has a name and a keys array.

    For RichCurves, each key has: time, value, interp_mode (Constant/Linear/Cubic),
    tangent_mode (Auto/User/Break/None), arrive_tangent, leave_tangent.

    For SimpleCurves, each key has: time, value. The interp_mode lives on the
    row itself (Constant or Linear only — Cubic is rejected by the engine).

    Args:
        asset_path: Full asset path to the CurveTable
            (e.g., "/Game/Data/CT_DamageFalloff").
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_curve_table", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_curve_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_curve_table_row(asset_path: str, row_name: str) -> Dict[str, Any]:
    """Read a single row of a UCurveTable by name.

    Args:
        asset_path: Full asset path to the CurveTable.
        row_name: Row name (FName key in the table).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_curve_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_curve_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_curve_table(
    name: str,
    curve_mode: str,
    package_path: str = "/Game/Data/",
    rows: Optional[List[Dict[str, Any]]] = None,
    save: bool = True
) -> Dict[str, Any]:
    """Create a new UCurveTable asset.

    A curve table is locked to one mode the first time a row is added.
    Pick RichCurves for authored curves with per-key tangents; pick
    SimpleCurves for lightweight time/value pairs with a single curve-wide
    interp mode.

    Row schema:
        {"name": "RowName",
         "keys": [
             {"time": 0.0, "value": 0.0, "interp_mode": "Linear"},
             {"time": 1.0, "value": 10.0}
         ],
         // SimpleCurves only — one interp_mode for the whole row:
         "interp_mode": "Linear"}

    Rich keys accept: time, value, interp_mode ("Constant"|"Linear"|"Cubic"),
    tangent_mode ("Auto"|"User"|"Break"|"None"), arrive_tangent, leave_tangent.
    Simple keys accept: time, value.

    Args:
        name: Asset name (no extension).
        curve_mode: "RichCurves" or "SimpleCurves".
        package_path: Content folder to create in (default "/Game/Data/").
        rows: Optional initial rows — see row schema above.
        save: Save the asset to disk after creation (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "name": name,
            "curve_mode": curve_mode,
            "package_path": package_path,
            "save": save,
        }
        if rows is not None:
            params["rows"] = rows
        response = unreal.send_command("create_curve_table", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_curve_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_curve_table_row(
    asset_path: str,
    row_name: str,
    keys: List[Dict[str, Any]],
    create_if_missing: bool = False,
    curve_mode: Optional[str] = None,
    interp_mode: Optional[str] = None,
    save: bool = True
) -> Dict[str, Any]:
    """Replace the keys on a single row of a UCurveTable.

    If the row exists, its keys are replaced wholesale. If the row does not
    exist and create_if_missing is true, the row is added. For empty tables,
    pass curve_mode to seed the mode when adding the first row.

    Args:
        asset_path: Full asset path to the CurveTable.
        row_name: Row name.
        keys: New key array (schema depends on curve_mode — see create_curve_table).
        create_if_missing: Add the row if it doesn't exist (default false).
        curve_mode: Required only when seeding an empty table.
        interp_mode: SimpleCurves only — curve-level interp mode for this row.
        save: Save the asset after updating (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "row_name": row_name,
            "keys": keys,
            "create_if_missing": create_if_missing,
            "save": save,
        }
        if curve_mode:
            params["curve_mode"] = curve_mode
        if interp_mode:
            params["interp_mode"] = interp_mode
        response = unreal.send_command("update_curve_table_row", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_curve_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_curve_table_row(
    asset_path: str,
    row_name: str,
    save: bool = True
) -> Dict[str, Any]:
    """Remove a row from a UCurveTable.

    Args:
        asset_path: Full asset path to the CurveTable.
        row_name: Row name to remove.
        save: Save the asset after deletion (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_curve_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
            "save": save,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_curve_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_curve_floats(
    directory: str = "/Game/",
    limit: int = 100
) -> Dict[str, Any]:
    """List UCurveFloat assets under a Content Browser directory.

    Standalone curve assets — not the rows inside a UCurveTable. Use this
    when you need a single named curve referenced from a property
    (e.g. recruitment discount curves, attribute scaling curves).

    Args:
        directory: Folder to search, recursive (default: "/Game/").
        limit: Maximum number of results to return.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_curve_floats", {
            "directory": directory,
            "limit": limit,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_curve_floats error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_curve_float(asset_path: str) -> Dict[str, Any]:
    """Read the keys of a UCurveFloat asset.

    Returns the inner FloatCurve as a list of rich keys: time, value,
    interp_mode (Constant/Linear/Cubic), tangent_mode (Auto/User/Break/None),
    arrive_tangent, leave_tangent.

    Args:
        asset_path: Full asset path to the CurveFloat
            (e.g., "/Game/Data/CRV_RecruitmentReputationDiscount").
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_curve_float", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_curve_float error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_curve_float(
    name: str,
    package_path: str = "/Game/Data/",
    keys: Optional[List[Dict[str, Any]]] = None,
    save: bool = True
) -> Dict[str, Any]:
    """Create a new UCurveFloat asset.

    A standalone single-curve asset. Use UCurveFloat for property-bound
    curves (recruitment discounts, damage falloff, attribute scaling).
    Use create_curve_table instead when you need many named rows of curves
    sharing one asset.

    Key schema (rich keys, same as create_curve_table RichCurves rows):
        {"time": 0.0, "value": 1.0,
         "interp_mode": "Linear",         # "Constant" | "Linear" | "Cubic"
         "tangent_mode": "Auto",          # "Auto" | "User" | "Break" | "None"
         "arrive_tangent": 0.0,           # optional
         "leave_tangent": 0.0}            # optional

    Args:
        name: Asset name (no extension, e.g. "CRV_RecruitmentReputationDiscount").
        package_path: Content folder (default "/Game/Data/").
        keys: Optional initial key array — see schema above.
        save: Save the asset to disk after creation (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "name": name,
            "package_path": package_path,
            "save": save,
        }
        if keys is not None:
            params["keys"] = keys
        response = unreal.send_command("create_curve_float", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_curve_float error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def update_curve_float(
    asset_path: str,
    keys: List[Dict[str, Any]],
    save: bool = True
) -> Dict[str, Any]:
    """Replace the keys on a UCurveFloat asset.

    Wholesale key replacement — pass the full desired key set, not a delta.
    Key schema matches create_curve_float.

    Args:
        asset_path: Full asset path to the CurveFloat.
        keys: New key array — see create_curve_float for schema.
        save: Save the asset after updating (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("update_curve_float", {
            "asset_path": asset_path,
            "keys": keys,
            "save": save,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"update_curve_float error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_data_table(asset_path: str, limit: int = 200) -> Dict[str, Any]:
    """Read rows from a UDataTable asset.

    Returns the row struct schema (field names and types) plus every row
    serialized to JSON via UPROPERTY reflection. Use this to inspect table
    contents before calling write_data_table_row — DataTable rows do NOT
    round-trip through read_data_asset (generic DataAsset read), so this
    is the only reliable way to observe row content.

    Args:
        asset_path: Full asset path to the DataTable
            (e.g., "/Game/PRK/Core/Data/Factions/DT_Attitude_Player").
        limit: Maximum rows to return (default 200).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_data_table", {
            "asset_path": asset_path,
            "limit": limit,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_data_table error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_data_table_row(asset_path: str, row_name: str) -> Dict[str, Any]:
    """Read a single row from a UDataTable by name.

    Returns the row struct type and the row's field values serialized to JSON.

    Args:
        asset_path: Full asset path to the DataTable.
        row_name: Row name (FName key).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_data_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def write_data_table_row(
    asset_path: str,
    row_name: str,
    fields: Dict[str, Any],
    create_if_missing: bool = True,
    merge: bool = True,
    save: bool = True
) -> Dict[str, Any]:
    """Create or update a single row of a UDataTable.

    The same property dispatcher used by update_data_asset applies here, so
    all field types are supported: bool, numeric, string, FName, FText,
    enums (name or value), TArray / TSet / TMap, TSoftObjectPtr, TSubclassOf,
    hard object refs (asset path), FGameplayTag (tag string),
    FGameplayTagContainer (array of tag strings), and nested struct objects.

    Existing rows are mutated in place; the table's HandleDataTableChanged
    is fired so listeners update. The written row is returned in `data` for
    verification (round-trip confirms what actually persisted).

    Args:
        asset_path: Full asset path to the DataTable.
        row_name: Row name (FName key). Created if absent and create_if_missing.
        fields: JSON object of row struct member values.
        create_if_missing: Add the row if it doesn't exist (default true).
        merge: When updating, preserve fields not in `fields` (default true).
            Pass false to reset the row to defaults before writing supplied fields.
        save: Save the asset after writing (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("write_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
            "fields": fields,
            "create_if_missing": create_if_missing,
            "merge": merge,
            "save": save,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"write_data_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def write_data_table_rows(
    asset_path: str,
    rows: Dict[str, Dict[str, Any]],
    create_if_missing: bool = True,
    merge: bool = True,
    clear_existing: bool = False,
    save: bool = True
) -> Dict[str, Any]:
    """Bulk write rows to a UDataTable.

    Each entry in `rows` is keyed by row name; the value is either the fields
    object directly, or {"fields": {...}} as an envelope.

    Example:
        rows = {
            "Hostile_Player": {
                "Attitude": "Hostile",
                "FactionTags": ["Factions.ID.RoadJackals", "Factions.ID.Chainwardens"]
            },
            "Neutral_Player": {
                "Attitude": "Neutral",
                "FactionTags": ["Factions.ID.CrossfallFreehold"]
            }
        }

    Args:
        asset_path: Full asset path to the DataTable.
        rows: Object mapping row name -> field values.
        create_if_missing: Add rows that don't exist (default true).
        merge: Preserve untouched fields on existing rows (default true).
        clear_existing: If true, empty the table before writing (default false).
        save: Save the asset after writing (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("write_data_table_rows", {
            "asset_path": asset_path,
            "rows": rows,
            "create_if_missing": create_if_missing,
            "merge": merge,
            "clear_existing": clear_existing,
            "save": save,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"write_data_table_rows error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_data_table_row(
    asset_path: str,
    row_name: str,
    save: bool = True
) -> Dict[str, Any]:
    """Remove a row from a UDataTable by name.

    Args:
        asset_path: Full asset path to the DataTable.
        row_name: Row name to remove.
        save: Save the asset after deletion (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_data_table_row", {
            "asset_path": asset_path,
            "row_name": row_name,
            "save": save,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_data_table_row error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def describe_data_asset_class(
    data_asset_class: str,
    include_inherited: bool = True
) -> Dict[str, Any]:
    """Return the UPROPERTY schema of a DataAsset class.

    Lists every property with its reflected type info: name, type, cpp_type,
    category, tooltip, ClampMin/Max/UIMin/UIMax meta, flags, enum values
    (for enum properties), element schema (for array/set/map), struct fields,
    and MetaClass/PropertyClass for class/object reference properties.

    Use this before create_data_asset or update_data_asset to discover what
    fields exist and what values they accept — avoids having to read headers.

    Args:
        data_asset_class: Short name or /Script/... path.
        include_inherited: Include properties from parent classes (default true).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("describe_data_asset_class", {
            "data_asset_class": data_asset_class,
            "include_inherited": include_inherited,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"describe_data_asset_class error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_data_asset_classes(
    name_contains: Optional[str] = None,
    limit: int = 500
) -> Dict[str, Any]:
    """Enumerate UDataAsset subclasses currently loaded in the editor.

    Useful for discovering which data asset classes exist before calling
    create_data_asset or describe_data_asset_class. Abstract, deprecated,
    and hot-reload-replaced classes are skipped.

    Args:
        name_contains: Optional substring filter against the class short name.
        limit: Maximum number of classes to return.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"limit": limit}
        if name_contains:
            params["name_contains"] = name_contains
        response = unreal.send_command("list_data_asset_classes", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_data_asset_classes error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def save_data_asset(asset_path: str, only_if_dirty: bool = True) -> Dict[str, Any]:
    """Save a Data Asset to disk.

    Args:
        asset_path: Full path to the DataAsset.
        only_if_dirty: If true, only writes when the package is dirty. Set
            false to force a save even if nothing appears changed.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("save_data_asset", {
            "asset_path": asset_path,
            "only_if_dirty": only_if_dirty,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"save_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_data_asset(asset_path: str) -> Dict[str, Any]:
    """Delete a Data Asset from the Content Browser.

    Args:
        asset_path: Full path to the DataAsset to delete.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_data_asset", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_data_asset(source_path: str, destination_path: str) -> Dict[str, Any]:
    """Rename or move a Data Asset. Fails if destination exists.

    Args:
        source_path: Current asset path.
        destination_path: New asset path (folder + new name).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("rename_data_asset", {
            "source_path": source_path,
            "destination_path": destination_path,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"rename_data_asset error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def duplicate_data_asset(source_path: str, destination_path: str) -> Dict[str, Any]:
    """Duplicate a Data Asset. Fails if destination exists.

    Args:
        source_path: Asset to copy.
        destination_path: Target path for the new copy.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("duplicate_data_asset", {
            "source_path": source_path,
            "destination_path": destination_path,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"duplicate_data_asset error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# StateTree Tools (Phase 1: lifecycle + skeleton)
# ============================================================================
#
# State addressing: `state`, `parent`, `new_parent` accept either an FGuid string
# (as returned by create/add calls) or a "/"-delimited path from a root subtree,
# e.g. "Root/Combat/Attack". First case-insensitive name match wins.
#
# `auto_compile` defaults False so you can batch edits and compile once at the
# end. Every mutation still runs ValidateStateTree internally.


@mcp.tool()
def create_state_tree(
    name: str,
    path: str = "/Game/StateTrees/",
    schema_class: str = "StateTreeComponentSchema",
) -> Dict[str, Any]:
    """Create a new UStateTree asset with a picked schema and a default Root subtree.

    Args:
        name: Asset name (no path).
        path: Package path (default /Game/StateTrees/).
        schema_class: UStateTreeSchema subclass name. Use list_state_tree_schemas to enumerate.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("create_state_tree", {
            "name": name,
            "path": path,
            "schema_class": schema_class,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_state_tree(asset_path: str, detailed: bool = False) -> Dict[str, Any]:
    """Serialize a StateTree to JSON.

    Args:
        asset_path: StateTree asset.
        detailed: When True, each state also includes full tasks/enter_conditions/considerations/
            transitions arrays (with node struct + instance field dumps) + global_tasks + evaluators.
            Otherwise only counts are returned. Also always reports root_parameters_id.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_state_tree", {
            "asset_path": asset_path, "detailed": detailed,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_state_trees(directory: str = "/Game/", limit: int = 200) -> Dict[str, Any]:
    """List UStateTree assets under a directory (recursive)."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_state_trees", {
            "directory": directory,
            "limit": limit,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_state_trees error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def save_state_tree(asset_path: str, only_if_dirty: bool = True) -> Dict[str, Any]:
    """Persist a StateTree to disk via UEditorAssetLibrary::SaveAsset."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("save_state_tree", {
            "asset_path": asset_path,
            "only_if_dirty": only_if_dirty,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"save_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_state_tree(asset_path: str) -> Dict[str, Any]:
    """Delete a StateTree asset. Fails if referenced."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_state_tree", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_state_tree(source_path: str, destination_path: str) -> Dict[str, Any]:
    """Rename/move a StateTree asset. Fails if destination exists."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("rename_state_tree", {
            "source_path": source_path,
            "destination_path": destination_path,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"rename_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def duplicate_state_tree(source_path: str, destination_path: str) -> Dict[str, Any]:
    """Duplicate a StateTree. Fails if destination exists."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("duplicate_state_tree", {
            "source_path": source_path,
            "destination_path": destination_path,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"duplicate_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def compile_state_tree(asset_path: str) -> Dict[str, Any]:
    """Compile a StateTree (UStateTreeEditingSubsystem::CompileStateTree). Reports last_compiled_hash."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("compile_state_tree", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"compile_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def validate_state_tree(asset_path: str) -> Dict[str, Any]:
    """Run schema validation + report whether the compiled hash matches the current editor-data hash."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("validate_state_tree", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"validate_state_tree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_state_tree_schemas() -> Dict[str, Any]:
    """Enumerate loaded UStateTreeSchema subclasses. Pass the returned `name` to create_state_tree."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("list_state_tree_schemas", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_state_tree_schemas error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_subtree(
    asset_path: str,
    name: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Add a top-level Subtree to a StateTree. Subtrees are link targets for Linked states.

    Args:
        asset_path: StateTree asset.
        name: Subtree display name.
        auto_compile: Recompile after mutation.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_subtree", {
            "asset_path": asset_path,
            "name": name,
            "auto_compile": auto_compile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_subtree error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_state_tree_state(
    asset_path: str,
    parent: str,
    name: str,
    state_type: str = "State",
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Add a child state under a parent state.

    Args:
        asset_path: StateTree asset.
        parent: Parent state GUID or path (e.g. "Root" or "Root/Combat").
        name: New state name.
        state_type: One of State, Group, Linked, LinkedAsset, Subtree.
        auto_compile: Recompile after mutation.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("add_state_tree_state", {
            "asset_path": asset_path,
            "parent": parent,
            "name": name,
            "state_type": state_type,
            "auto_compile": auto_compile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"add_state_tree_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def remove_state_tree_state(
    asset_path: str,
    state: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Remove a state (and its children) from a StateTree.

    Args:
        asset_path: StateTree asset.
        state: State GUID or path.
        auto_compile: Recompile after mutation.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("remove_state_tree_state", {
            "asset_path": asset_path,
            "state": state,
            "auto_compile": auto_compile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"remove_state_tree_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def rename_state_tree_state(
    asset_path: str,
    state: str,
    new_name: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Rename a state. Note: path-based addressing for descendants will shift after rename."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("rename_state_tree_state", {
            "asset_path": asset_path,
            "state": state,
            "new_name": new_name,
            "auto_compile": auto_compile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"rename_state_tree_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def move_state_tree_state(
    asset_path: str,
    state: str,
    new_parent: str,
    index: Optional[int] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Reparent a state under a new parent. Rejects cycles.

    Args:
        asset_path: StateTree asset.
        state: State to move (GUID or path).
        new_parent: Destination parent (GUID or path).
        index: Insertion index inside new_parent.Children; default = append.
        auto_compile: Recompile after mutation.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params = {
            "asset_path": asset_path,
            "state": state,
            "new_parent": new_parent,
            "auto_compile": auto_compile,
        }
        if index is not None:
            params["index"] = index
        response = unreal.send_command("move_state_tree_state", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"move_state_tree_state error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_state_tree_state_property(
    asset_path: str,
    state: str,
    properties: Dict[str, Any],
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Set scalar state properties via reflection.

    Supported fields include: Name, Description, Tag, Type, SelectionBehavior,
    TasksCompletion, Weight, bEnabled, bHasCustomTickRate, CustomTickRate.
    Tasks/Conditions/Transitions/Bindings are out of scope for Phase 1 — use
    the dedicated commands once Phase 2 lands.

    Args:
        asset_path: StateTree asset.
        state: State GUID or path.
        properties: Field -> value dict. Applied via FEpicUnrealMCPCommonUtils::SetObjectProperty.
        auto_compile: Recompile after mutation.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_state_tree_state_property", {
            "asset_path": asset_path,
            "state": state,
            "properties": properties,
            "auto_compile": auto_compile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_state_tree_state_property error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# StateTree Tools (Phase 2: tasks, conditions, considerations, transitions,
#                   global tasks, evaluators)
# ============================================================================
#
# struct_path addressing: pass the node's UScriptStruct as a short name like
# "StateTreeRunEnvQueryTask" or a script path "/Script/StateTreeModule.StateTreeRunEnvQueryTask".
# The F-prefix is added automatically if missing.
#
# Every node's Task/Condition struct has a paired Instance struct. Use:
#   node_properties     → set fields on the Node struct (the task/condition logic)
#   instance_properties → set fields on the Instance struct (the task inputs/outputs)
# Both default to empty.
#
# Returned node_id is the FGuid to pass to remove_state_tree_node /
# set_state_tree_node_property / add_property_binding.


def _state_tree_add_node(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command(command, params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"{command} error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def add_state_task(
    asset_path: str,
    state: str,
    struct_path: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Append a task to a state's Tasks list.

    Args:
        asset_path: StateTree asset.
        state: State GUID or path.
        struct_path: UScriptStruct name (short or script path) derived from FStateTreeTaskBase.
        node_properties: Optional field dict applied to the task Node struct.
        instance_properties: Optional field dict applied to the task Instance struct.
        auto_compile: Recompile after mutation.
    """
    p = {"asset_path": asset_path, "state": state, "struct_path": struct_path, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    return _state_tree_add_node("add_state_task", p)


@mcp.tool()
def add_state_enter_condition(
    asset_path: str,
    state: str,
    struct_path: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Append an enter condition to a state (FStateTreeConditionBase derivative)."""
    p = {"asset_path": asset_path, "state": state, "struct_path": struct_path, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    return _state_tree_add_node("add_state_enter_condition", p)


@mcp.tool()
def add_state_consideration(
    asset_path: str,
    state: str,
    struct_path: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Append a utility consideration to a state (FStateTreeConsiderationBase derivative)."""
    p = {"asset_path": asset_path, "state": state, "struct_path": struct_path, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    return _state_tree_add_node("add_state_consideration", p)


@mcp.tool()
def add_global_task(
    asset_path: str,
    struct_path: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Append a global task (runs for the lifetime of the StateTree)."""
    p = {"asset_path": asset_path, "struct_path": struct_path, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    return _state_tree_add_node("add_global_task", p)


@mcp.tool()
def add_evaluator(
    asset_path: str,
    struct_path: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Append an evaluator (shared source of values for bindings)."""
    p = {"asset_path": asset_path, "struct_path": struct_path, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    return _state_tree_add_node("add_evaluator", p)


@mcp.tool()
def remove_state_tree_node(
    asset_path: str,
    node_id: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Remove a node (task/condition/consideration/transition-condition/global/evaluator) by GUID.

    Args:
        asset_path: StateTree asset.
        node_id: FGuid returned from the add_* call.
        auto_compile: Recompile after mutation.
    """
    return _state_tree_add_node("remove_state_tree_node", {
        "asset_path": asset_path, "node_id": node_id, "auto_compile": auto_compile,
    })


@mcp.tool()
def set_state_tree_node_property(
    asset_path: str,
    node_id: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    editor_node_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Update a node in-place.

    Args:
        asset_path: StateTree asset.
        node_id: Node FGuid.
        node_properties: Fields on the Node struct (task/condition logic).
        instance_properties: Fields on the Instance struct (task inputs/outputs).
        editor_node_properties: Fields on FStateTreeEditorNode itself (ExpressionIndent, ExpressionOperand).
        auto_compile: Recompile after mutation.
    """
    p = {"asset_path": asset_path, "node_id": node_id, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    if editor_node_properties is not None: p["editor_node_properties"] = editor_node_properties
    return _state_tree_add_node("set_state_tree_node_property", p)


@mcp.tool()
def add_state_tree_transition(
    asset_path: str,
    state: str,
    target_state: Optional[str] = None,
    trigger: str = "OnStateCompleted",
    link_type: str = "GotoState",
    required_event_tag: Optional[str] = None,
    priority: Optional[str] = None,
    delay_duration: Optional[float] = None,
    delay_random_variance: Optional[float] = None,
    enabled: bool = True,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Add a transition to a state.

    Args:
        asset_path: StateTree asset.
        state: Source state (GUID or path).
        target_state: Destination state (GUID or path). Required when link_type is GotoState.
        trigger: EStateTreeTransitionTrigger name (e.g. OnStateCompleted, OnTick, OnEvent).
        link_type: EStateTreeTransitionType (GotoState, Succeeded, Failed, NextState, NextSelectableState).
        required_event_tag: Optional gameplay tag that must be present to trigger (e.g. "Event.Damaged").
        priority: EStateTreeTransitionPriority (Low/Normal/Medium/High/Critical).
        delay_duration: Seconds to wait before firing. Enables bDelayTransition when > 0.
        delay_random_variance: +/- random seconds added to delay_duration.
        enabled: Start enabled.
        auto_compile: Recompile after mutation.
    """
    p = {"asset_path": asset_path, "state": state, "trigger": trigger, "link_type": link_type,
         "enabled": enabled, "auto_compile": auto_compile}
    if target_state is not None: p["target_state"] = target_state
    if required_event_tag is not None: p["required_event_tag"] = required_event_tag
    if priority is not None: p["priority"] = priority
    if delay_duration is not None: p["delay_duration"] = delay_duration
    if delay_random_variance is not None: p["delay_random_variance"] = delay_random_variance
    return _state_tree_add_node("add_state_tree_transition", p)


@mcp.tool()
def remove_state_tree_transition(
    asset_path: str,
    state: str,
    transition_id: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Remove a transition from a state by its FGuid."""
    return _state_tree_add_node("remove_state_tree_transition", {
        "asset_path": asset_path, "state": state, "transition_id": transition_id,
        "auto_compile": auto_compile,
    })


@mcp.tool()
def set_state_tree_transition_property(
    asset_path: str,
    state: str,
    transition_id: str,
    properties: Dict[str, Any],
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Update FStateTreeTransition scalar fields in-place (Trigger, Priority, bDelayTransition, etc.)."""
    return _state_tree_add_node("set_state_tree_transition_property", {
        "asset_path": asset_path, "state": state, "transition_id": transition_id,
        "properties": properties, "auto_compile": auto_compile,
    })


@mcp.tool()
def add_transition_condition(
    asset_path: str,
    state: str,
    transition_id: str,
    struct_path: str,
    node_properties: Optional[Dict[str, Any]] = None,
    instance_properties: Optional[Dict[str, Any]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Append a condition node to a transition's Conditions array."""
    p = {"asset_path": asset_path, "state": state, "transition_id": transition_id,
         "struct_path": struct_path, "auto_compile": auto_compile}
    if node_properties is not None: p["node_properties"] = node_properties
    if instance_properties is not None: p["instance_properties"] = instance_properties
    return _state_tree_add_node("add_transition_condition", p)


# ============================================================================
# StateTree Tools (Phase 3: root parameters + property bindings)
# ============================================================================


@mcp.tool()
def add_root_parameter(
    asset_path: str,
    name: str,
    type: str,
    value_type: Optional[str] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Add a parameter to the StateTree's root parameter bag (bindable from any node).

    Args:
        asset_path: StateTree asset.
        name: Parameter name (FName).
        type: Primitive type — bool/byte/int32/int64/float/double/name/string/text/enum/struct/object/softobject/class/softclass.
        value_type: For enum/struct/object/class/softobject/softclass: the type's short name or path.
        auto_compile: Recompile after mutation.
    """
    p = {"asset_path": asset_path, "name": name, "type": type, "auto_compile": auto_compile}
    if value_type is not None: p["value_type"] = value_type
    return _state_tree_add_node("add_root_parameter", p)


@mcp.tool()
def remove_root_parameter(
    asset_path: str,
    name: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Remove a parameter from the StateTree root parameter bag."""
    return _state_tree_add_node("remove_root_parameter", {
        "asset_path": asset_path, "name": name, "auto_compile": auto_compile,
    })


@mcp.tool()
def set_root_parameter_value(
    asset_path: str,
    name: str,
    value: Any,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Set a root parameter's value. Supports JSON bool/number/string. Complex types not yet supported."""
    return _state_tree_add_node("set_root_parameter_value", {
        "asset_path": asset_path, "name": name, "value": value, "auto_compile": auto_compile,
    })


@mcp.tool()
def add_property_binding(
    asset_path: str,
    source_struct_id: str,
    target_struct_id: str,
    source_path: str = "",
    target_path: str = "",
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Bind one struct property (source) to another (target).

    Source/target struct IDs are FGuids. The root parameter bag has an ID you can fetch
    via analyze_state_tree → counts.root_parameters / read_state_tree shows state IDs;
    task/condition/consideration nodes return IDs from their add_* call.

    Args:
        asset_path: StateTree asset.
        source_struct_id: FGuid of the source struct (root params, task node, etc.).
        target_struct_id: FGuid of the target struct (usually a task's Instance node GUID).
        source_path: Dotted property path inside the source struct (e.g. "Speed" or "Payload.Count").
        target_path: Dotted property path inside the target struct.
        auto_compile: Recompile after mutation.
    """
    return _state_tree_add_node("add_property_binding", {
        "asset_path": asset_path,
        "source_struct_id": source_struct_id,
        "target_struct_id": target_struct_id,
        "source_path": source_path,
        "target_path": target_path,
        "auto_compile": auto_compile,
    })


@mcp.tool()
def remove_property_binding(
    asset_path: str,
    target_struct_id: str,
    target_path: str = "",
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Remove all bindings pointing at the given target path."""
    return _state_tree_add_node("remove_property_binding", {
        "asset_path": asset_path,
        "target_struct_id": target_struct_id,
        "target_path": target_path,
        "auto_compile": auto_compile,
    })


@mcp.tool()
def list_property_bindings(asset_path: str) -> Dict[str, Any]:
    """Enumerate all editor property bindings in the StateTree."""
    return _state_tree_add_node("list_property_bindings", {"asset_path": asset_path})


# ============================================================================
# StateTree Tools (Phase 4: analysis)
# ============================================================================


@mcp.tool()
def analyze_state_tree(
    asset_path: str,
    run_compile: bool = False,
) -> Dict[str, Any]:
    """Structural analysis. Returns JSON with:
      - counts (states/tasks/transitions/global tasks/evaluators/root_parameters/bindings)
      - compile.editor_up_to_date, compile.editor_data_hash, compile.last_compiled_hash
      - broken_transitions: transitions whose GotoState target GUID is no longer present
      - orphan_bindings: bindings whose source/target struct GUID does not exist in the tree

    Args:
        asset_path: StateTree asset.
        run_compile: If true, also compile and return tokenized messages.
    """
    return _state_tree_add_node("analyze_state_tree", {
        "asset_path": asset_path, "run_compile": run_compile,
    })


# ============================================================================
# StateTree Tools (Phase 5: BP nodes, node class discovery, state parameters,
#                  complex parameter values, advanced bindings)
# ============================================================================


@mcp.tool()
def list_state_tree_node_classes(category: str = "all") -> Dict[str, Any]:
    """Enumerate native C++ node structs AND Blueprint subclasses for each category.

    Returned entries include a `path` (script or BP asset path) you can pass to
    add_state_task / add_state_enter_condition / add_state_consideration /
    add_global_task / add_evaluator as `struct_path`.

    Args:
        category: all, task, condition, consideration, evaluator.
    """
    return _state_tree_add_node("list_state_tree_node_classes", {"category": category})


@mcp.tool()
def add_state_parameter(
    asset_path: str,
    state: str,
    name: str,
    type: str,
    value_type: Optional[str] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Add a parameter to a state's local parameter bag.

    State parameters feed into Linked / LinkedAsset states so child trees receive values.

    Args:
        asset_path: StateTree asset.
        state: State GUID or path.
        name: Parameter name.
        type: bool/byte/int32/int64/float/double/name/string/text/enum/struct/object/softobject/class/softclass.
        value_type: For enum/struct/object/class/softobject/softclass: the type's short name or path.
        auto_compile: Recompile after mutation.
    """
    p = {"asset_path": asset_path, "state": state, "name": name, "type": type, "auto_compile": auto_compile}
    if value_type is not None: p["value_type"] = value_type
    return _state_tree_add_node("add_state_parameter", p)


@mcp.tool()
def remove_state_parameter(
    asset_path: str,
    state: str,
    name: str,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Remove a parameter from a state's local parameter bag."""
    return _state_tree_add_node("remove_state_parameter", {
        "asset_path": asset_path, "state": state, "name": name, "auto_compile": auto_compile,
    })


@mcp.tool()
def set_state_parameter_value(
    asset_path: str,
    state: str,
    name: str,
    value: Any,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Set a state parameter's value. Handles primitives, enums (string or int),
    structs (JSON object), objects/classes (string asset or class path)."""
    return _state_tree_add_node("set_state_parameter_value", {
        "asset_path": asset_path, "state": state, "name": name, "value": value,
        "auto_compile": auto_compile,
    })


@mcp.tool()
def add_function_binding(
    asset_path: str,
    function_struct: str,
    target_struct_id: str,
    target_path: str = "",
    source_path_segments: Optional[List[str]] = None,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Bind a property via a PropertyFunction struct (e.g. FStateTreeMultiplyFloat).

    Args:
        asset_path: StateTree asset.
        function_struct: UScriptStruct name/path of the property function.
        target_struct_id: Destination struct FGuid (usually a task/state parameter).
        target_path: Dotted path inside the target struct.
        source_path_segments: Optional path inside the function's output struct.
        auto_compile: Recompile after mutation.

    Returns the generated source struct GUID/path so you can chain it.
    """
    p = {"asset_path": asset_path, "function_struct": function_struct,
         "target_struct_id": target_struct_id, "target_path": target_path,
         "auto_compile": auto_compile}
    if source_path_segments is not None: p["source_path_segments"] = source_path_segments
    return _state_tree_add_node("add_function_binding", p)


@mcp.tool()
def add_output_binding(
    asset_path: str,
    source_struct_id: str,
    target_struct_id: str,
    source_path: str = "",
    target_path: str = "",
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Add an output binding (copies target→source, inverse of a normal binding).

    Used when a task writes an output that a later evaluator/consumer reads from.
    """
    return _state_tree_add_node("add_output_binding", {
        "asset_path": asset_path,
        "source_struct_id": source_struct_id,
        "target_struct_id": target_struct_id,
        "source_path": source_path,
        "target_path": target_path,
        "auto_compile": auto_compile,
    })


# ============================================================================
# StateTree Tools (Phase 6: schema introspection, linked states, parameter overrides)
# ============================================================================


@mcp.tool()
def list_schema_context_data(schema_class: str) -> Dict[str, Any]:
    """Report the context struct types + capability flags a given schema permits.

    Useful for diagnosing 'External data of type X is not allowed by schema Y' compile
    errors: the returned context_data array enumerates exactly which struct types the
    schema's IsExternalItemAllowed will accept.

    Args:
        schema_class: UStateTreeSchema subclass short name or script path.
    """
    return _state_tree_add_node("list_schema_context_data", {"schema_class": schema_class})


@mcp.tool()
def set_linked_state(
    asset_path: str,
    state: str,
    target_subtree: Optional[str] = None,
    target_asset_path: Optional[str] = None,
    clear: bool = False,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Turn a state into a Linked / LinkedAsset state.

    Exactly one of target_subtree / target_asset_path / clear must be supplied.

    Args:
        asset_path: StateTree asset.
        state: State GUID or path. Its Type will be updated to Linked or LinkedAsset.
        target_subtree: A Subtree-type state in the SAME asset (by GUID or path).
            Sets state Type=Linked, LinkedSubtree pointing at the target.
        target_asset_path: Package path of another UStateTree asset.
            Sets state Type=LinkedAsset, LinkedAsset pointing there. Rejects self-links.
        clear: Reverts state back to Type=State and clears any link + parameter overrides.
        auto_compile: Recompile after mutation.
    """
    p = {"asset_path": asset_path, "state": state, "auto_compile": auto_compile}
    if target_subtree is not None: p["target_subtree"] = target_subtree
    if target_asset_path is not None: p["target_asset_path"] = target_asset_path
    if clear: p["clear"] = True
    return _state_tree_add_node("set_linked_state", p)


@mcp.tool()
def set_state_parameter_override(
    asset_path: str,
    state: str,
    parameter_name: str,
    overridden: bool,
    auto_compile: bool = False,
) -> Dict[str, Any]:
    """Toggle whether a state parameter overrides the linked subtree's / asset's default.

    For Linked / LinkedAsset states: parameters not marked as overridden inherit from
    the linked target. Marking overridden=true means the local parameter value wins.

    Args:
        asset_path: StateTree asset.
        state: State GUID or path.
        parameter_name: Name inside the state's parameter bag.
        overridden: True = local value wins, False = inherit from linked target.
        auto_compile: Recompile after mutation.
    """
    return _state_tree_add_node("set_state_parameter_override", {
        "asset_path": asset_path, "state": state,
        "parameter_name": parameter_name, "overridden": overridden,
        "auto_compile": auto_compile,
    })


# ============================================================================
# Blueprint CDO default-value editing
# ============================================================================


@mcp.tool()
def get_blueprint_default_value(
    blueprint_name: str,
    property_name: str,
) -> Dict[str, Any]:
    """Read the current Class Default Object value for a UPROPERTY on a Blueprint.

    Returns value as a string via FProperty::ExportText (matches the editor
    representation). For TObjectPtr properties that's the asset package path.

    Args:
        blueprint_name: Short name or full /Game/... package path.
        property_name: UPROPERTY field name on the blueprint's generated class.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_blueprint_default_value", {
            "blueprint_name": blueprint_name,
            "property_name": property_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_blueprint_default_value error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_blueprint_default_value(
    blueprint_name: str,
    property_name: str,
    value: Any,
    recompile: bool = True,
) -> Dict[str, Any]:
    """Override a UPROPERTY default on a Blueprint's CDO.

    Edits the Class Default Object on Blueprint.GeneratedClass. Replaces the
    native constructor default OR the parent class's override. Marks the
    Blueprint dirty. By default also recompiles so the change takes effect
    without a manual compile step.

    Typical use:
        # Assign ST_PRKCombat to a task's StateTree asset property.
        set_blueprint_default_value(
            "BP_AthenaCombat_StateTreeEnabled",
            "CombatStateTreeAsset",
            "/Game/PRK/AI/StateTrees/ST_PRKCombat.ST_PRKCombat")

    Args:
        blueprint_name: Short name or full /Game/... package path.
        property_name: UPROPERTY field name.
        value: New value. Matches FProperty JSON conventions:
            - bool/number/string → primitives
            - TObjectPtr / soft refs → asset package path string
            - TSubclassOf<T> (FClassProperty) → class path string, OR
              ``None`` / ``"None"`` / ``""`` to clear (e.g. clearing
              GameMode ``HUDClass``: pass ``value=None``).
            - structs → JSON object (routed through SetObjectProperty)
            - arrays → JSON array
        recompile: Recompile the Blueprint after mutation (default True).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_blueprint_default_value", {
            "blueprint_name": blueprint_name,
            "property_name": property_name,
            "value": value,
            "recompile": recompile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_blueprint_default_value error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reset_blueprint_default_value(
    blueprint_name: str,
    property_name: str,
    recompile: bool = True,
) -> Dict[str, Any]:
    """Revert a property override — copies the parent class's CDO value back.

    Useful when a Blueprint override is no longer desired and you want to
    inherit the parent's default again.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("reset_blueprint_default_value", {
            "blueprint_name": blueprint_name,
            "property_name": property_name,
            "recompile": recompile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reset_blueprint_default_value error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Component archetype default-value editing (inherited subobjects + SCS)
# ============================================================================
#
# `set_blueprint_default_value` only walks BP CDO direct properties. Any
# property that lives on a default subobject — i.e. anything you'd edit in
# the "Inherited Components" details panel (mesh anim class, component-owned
# arrays/structs like StartingItems on a LoadoutComponent) — needs these.


@mcp.tool()
def get_component_default_value(
    blueprint_name: str,
    component_name: str,
    property_name: str,
) -> Dict[str, Any]:
    """Read a UPROPERTY value on a Blueprint's component archetype.

    Resolves the component template by name. Handles BOTH:
      - Inherited C++ default subobjects (CreateDefaultSubobject in a parent
        class constructor — e.g. "LoadoutComp" on APRKPawn).
      - SCS-added components on this BP or any parent BP in the chain.

    Use this when the property does NOT live on the BP class itself but on
    one of its components — `get_blueprint_default_value` will fail with
    "Property not found on class" for those.

    Args:
        blueprint_name: Short name or full /Game/... package path.
        component_name: Subobject name (e.g. "LoadoutComp", "Mesh", "Camera").
        property_name: UPROPERTY field name on the component class.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_component_default_value", {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "property_name": property_name,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_component_default_value error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_component_default_value(
    blueprint_name: str,
    component_name: str,
    property_name: str,
    value: Any,
    recompile: bool = True,
) -> Dict[str, Any]:
    """Override a UPROPERTY default on a Blueprint's component archetype.

    Edits the component template/archetype owned by the BP. After mutation
    the BP is marked modified and (by default) recompiled so existing
    instances are re-instanced with the new defaults.

    Typical use — set the StartingItems array on a Pawn BP's inherited
    LoadoutComp without needing to open the editor's details panel:

        set_component_default_value(
            "/Game/PRK/Character/Blueprints/BP_PRK_Pawn.BP_PRK_Pawn",
            "LoadoutComp",
            "StartingItems",
            [{
                "ItemData": "/Game/PRK/Items/Weapons/Sword/DA_Weapon_Sword_FootmansBlade.DA_Weapon_Sword_FootmansBlade",
                "Quantity": 1,
                "EquipSlot": "Equipment.Weapon.MainHand.Set1",
                "WeaponSetIndex": 0,
            }, ...])

    Args:
        blueprint_name: Short name or full /Game/... package path.
        component_name: Subobject name (e.g. "LoadoutComp", "Mesh").
        property_name: UPROPERTY field on the component class.
        value: Same JSON conventions as `set_blueprint_default_value`
            (primitives / asset path string / struct dict / array).
        recompile: Recompile the BP after mutation (default True).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_component_default_value", {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "property_name": property_name,
            "value": value,
            "recompile": recompile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_component_default_value error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def reset_component_default_value(
    blueprint_name: str,
    component_name: str,
    property_name: str,
    recompile: bool = True,
) -> Dict[str, Any]:
    """Revert a component override — copies the archetype's source value back.

    Mirrors `reset_blueprint_default_value` but for a property on a component
    template. Restores the inherited default (the parent class's archetype).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("reset_component_default_value", {
            "blueprint_name": blueprint_name,
            "component_name": component_name,
            "property_name": property_name,
            "recompile": recompile,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"reset_component_default_value error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def run_python(
    code: str,
    mode: str = "ExecuteFile",
    scope: str = "Public",
    unattended: bool = True,
) -> Dict[str, Any]:
    """Execute Python source inside the running Unreal editor.

    Runs on the editor's game thread via IPythonScriptPlugin::ExecPythonCommandEx.
    Captures stdout, stderr, and the eval result; returns the Python exception
    trace on failure.

    Args:
        code: Python source. May be multi-statement when ``mode`` is ExecuteFile.
        mode: One of ``ExecuteFile`` (default, multi-statement), ``ExecuteStatement``
            (single statement, prints result), or ``EvaluateStatement`` (returns
            the expression value in the ``result`` field).
        scope: ``Public`` (default) shares globals with the in-editor Python
            console — subsequent calls see prior imports and variables.
            ``Private`` isolates the call's globals/locals.
        unattended: If True (default), sets ``GIsRunningUnattendedScript`` so
            modal dialogs are suppressed.

    Returns:
        ``{success, result, stdout, stderr, log: [{type, text}, ...]}``.
        ``log`` preserves the original ordering and Info/Warning/Error type per
        entry; ``stdout``/``stderr`` are pre-flattened convenience buckets.

    Deny-list (refused here, never sent to the editor; helpers/prk_python_guard.py):
    texture exporters, AssetExportTask on a texture, ForceUpdateAllLandscape,
    StartEditMode2/EndEditMode2, ElBridge PerLandscapeSettings / WeightmapMethod writes, and
    spawning/destroying Errant Landscape brushes (use errant_stamp_spawn / errant_stamp_delete).
    The refusal names the safe alternative.
    """
    refused = prk_python_guard.refusal(prk_python_guard.check_python_source(code), "run_python code")
    if refused:
        return refused
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("run_python", {
            "code": code,
            "mode": mode,
            "scope": scope,
            "unattended": unattended,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"run_python error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def run_python_file(
    file_path: str,
    args: str = "",
    scope: str = "Public",
    unattended: bool = True,
) -> Dict[str, Any]:
    """Execute a .py file inside the running Unreal editor.

    The file path is resolved on the editor host's filesystem (the editor reads
    it directly — this is not a file upload). Output capture matches
    ``run_python``.

    Args:
        file_path: Absolute or editor-relative path to a .py file.
        args: Optional argument string appended after the file path. Parsed by
            the Python plugin the same way the in-editor ``py`` console command
            parses arguments.
        scope: ``Public`` (default) or ``Private`` — see ``run_python``.
        unattended: Suppress modal dialogs (default True).

    The file is read here first and checked against the ``run_python`` deny-list; a
    file this server cannot read (relative to the editor only) is refused, so pass an
    absolute path.
    """
    source_path = None
    for cand in (file_path,
                 os.path.join(prk_texture_preview.project_root() or "", file_path),
                 os.path.join(prk_texture_preview.project_root() or "", "Content", "Python", file_path)):
        if cand and os.path.isfile(cand):
            source_path = cand
            break
    if source_path is None:
        return {"success": False, "refused": True,
                "message": f"run_python_file: cannot read '{file_path}' to check it against the deny-list; "
                           "pass an absolute path."}
    try:
        with open(source_path, "r", encoding="utf-8", errors="replace") as f:
            source = f.read()
    except OSError as e:
        return {"success": False, "refused": True, "message": f"run_python_file: cannot read '{source_path}': {e}"}
    refused = prk_python_guard.refusal(prk_python_guard.check_python_source(source), f"file {source_path}")
    if refused:
        return refused
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("run_python_file", {
            "file_path": file_path,
            "args": args,
            "scope": scope,
            "unattended": unattended,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"run_python_file error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def fixup_redirectors(
    path: str = "/Game",
    recursive: bool = True,
    delete_redirectors: bool = True,
) -> Dict[str, Any]:
    """Rewrite all references to ObjectRedirectors under ``path`` and (optionally) delete the stubs.

    Equivalent to right-clicking a folder in the Content Browser and choosing
    "Fix Up Redirectors in Folder", but driven from MCP. Calls
    ``IAssetTools::FixupReferencers`` directly — no Python plugin involvement.

    Args:
        path: Content-relative package path (default ``/Game``).
        recursive: Include subfolders (default True).
        delete_redirectors: After successful fixup, delete the redirector
            assets (default True). Set False to leave the stubs in place
            (``LeaveFixedUpRedirectors`` mode).

    Returns:
        ``{success, path, recursive, redirectors_found, ran_fixup, delete_redirectors}``.
        ``ran_fixup`` is False when no redirectors were found.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("fixup_redirectors", {
            "path": path,
            "recursive": recursive,
            "delete_redirectors": delete_redirectors,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"fixup_redirectors error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def delete_blueprint(
    asset_path: str,
    allow_widget: bool = False,
) -> Dict[str, Any]:
    """Delete a generic ``UBlueprint`` asset (Actor / Pawn / HUD / GameMode / etc.).

    The companion to ``delete_widget_blueprint`` — this one rejects widget BPs
    by default so the audit trail stays separated by asset family. Pass
    ``allow_widget=True`` for a single delete entry point regardless of BP type.

    Args:
        asset_path: Content-relative path, e.g. ``/Game/UI/HUD/BP_PRK_Hud``.
        allow_widget: Allow deletion of ``UWidgetBlueprint`` through this entry
            point (default False — prefer ``delete_widget_blueprint``).

    Returns:
        ``{success, asset_path, deleted, blueprint_class[, error]}``.

    Notes:
        If deletion fails the asset still has live references. Call
        ``find_asset_referencers`` to identify holders, or
        ``replace_asset_references`` to redirect them first.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("delete_blueprint", {
            "asset_path": asset_path,
            "allow_widget": allow_widget,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"delete_blueprint error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def find_asset_referencers(
    asset_path: str,
    transitive: bool = False,
    max_results: int = 1024,
) -> Dict[str, Any]:
    """List packages that reference a given asset (read-only).

    Wraps ``IAssetRegistry::GetReferencers`` — same data the Reference Viewer
    panel reads. Default is direct referencers only; ``transitive=True`` walks
    the dependency graph (BFS, deduped) so you can see the full blast radius
    before a delete or reference-replacement.

    Args:
        asset_path: ``/Game/.../Asset.Asset`` or ``/Game/.../Asset`` (the
            trailing ``.Name`` is stripped).
        transitive: Include indirect referencers (default False).
        max_results: Cap the result list (default 1024). Response sets
            ``truncated=True`` when reached.

    Returns:
        ``{success, asset_path, transitive, count, truncated, referencers}``
        where ``referencers`` is a list of package names.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("find_asset_referencers", {
            "asset_path": asset_path,
            "transitive": transitive,
            "max_results": max_results,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"find_asset_referencers error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def replace_asset_references(
    old_asset_path: str,
    new_asset_path: str,
    allow_different_class: bool = False,
) -> Dict[str, Any]:
    """Redirect every reference of ``old_asset_path`` to ``new_asset_path``, then delete the old asset.

    Equivalent to right-clicking an asset in the Content Browser and choosing
    "Replace References". Calls ``ObjectTools::ConsolidateObjects`` directly.
    An ``ObjectRedirector`` is left behind for any unloaded consumer packages —
    follow up with ``fixup_redirectors`` to scrub those once consumer packages
    have been touched.

    Args:
        old_asset_path: Asset to remove (its references will be rewritten).
        new_asset_path: Asset that will receive the redirected references.
        allow_different_class: Permit consolidation when old/new classes
            differ (default False — refuses a Widget→Actor swap, etc.). The
            engine itself does no type-checking, so use this only when you
            understand the swap is safe (e.g. swapping one ``AHUD`` subclass
            for another, or two widget BPs of different parents).

    Returns:
        ``{success, old_asset_path, new_asset_path, dirtied_packages,
        invalid_count, failed_count, dirtied_package_names, note}``.

    Notes:
        Unloaded consumer packages keep referencing through the redirector
        until they are loaded and resaved. The fixup_redirectors call is what
        finishes the job.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("replace_asset_references", {
            "old_asset_path": old_asset_path,
            "new_asset_path": new_asset_path,
            "allow_different_class": allow_different_class,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"replace_asset_references error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# AI Icon Generation (Google AI Studio / Imagen)
# ============================================================================
#
# generate_icon_asset calls Google AI Studio's Imagen API, writes the resulting
# PNG to a temp file, then asks the UE side to import it as a Texture2D with
# UI-icon-friendly settings (UI texture group, sRGB, UserInterface2D
# compression, no mips, never streamed).
#
# Auth: requires the GOOGLE_AI_API_KEY env var (read at request time, not
# at server start, so editing the env without restarting still works on the
# *next* MCP launch — the running server captures env at process start).
#
# Pricing: each Imagen call costs ~$0.03-$0.04 USD. Use dry_run=True to
# inspect the constructed prompt without spending budget.

import base64
import tempfile

# Background-removal strategies for icon post-processing.
#   "none"     — leave the PNG as-is.
#   "chroma"   — pure-Python flood-fill from the four image corners over any
#                pixel within `chroma_tolerance` of the sampled corner color.
#                Fast, no extra deps, works perfectly for Imagen's typical
#                solid white/grey/black backgrounds. Fails on busy backgrounds
#                or when the icon touches the canvas edge.
#   "rembg"    — run the `rembg` library if installed. Handles arbitrary
#                backgrounds (uses an ONNX U2Net model under the hood).
#                Slower (1-3s on CPU, ~5s first call to load the model).
_BG_REMOVAL_MODES = {"none", "chroma", "rembg"}


def _remove_background_chroma(
    png_bytes: bytes,
    tolerance: int = 18,
) -> bytes:
    """Flood-fill from the four corners over background-colored pixels and
    write transparent alpha there. Pure stdlib (no Pillow / numpy required —
    falls back to Pillow if available for ~10x speedup).

    Returns the new PNG bytes, or the original bytes if the algorithm can't
    decode the input or no background-colored pixels are found at the corners.
    """
    try:
        from PIL import Image
    except ImportError:
        # Without Pillow we can't decode arbitrary PNGs from stdlib. Return
        # the input unchanged and let the caller decide whether that's fatal.
        logger.warning(
            "chroma background removal requires Pillow. "
            "Install with: pip install pillow"
        )
        return png_bytes

    import io

    try:
        img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    except Exception as e:
        logger.warning(f"chroma bg-remove: failed to decode PNG: {e}")
        return png_bytes

    width, height = img.size
    pixels = img.load()

    # Sample the four corners; each becomes a flood-fill seed. We collect
    # multiple seeds because Imagen sometimes paints subtly different bg
    # colors at top vs bottom (gradient backgrounds).
    seeds = [
        (0, 0),
        (width - 1, 0),
        (0, height - 1),
        (width - 1, height - 1),
    ]

    def color_close(a, b) -> bool:
        return (
            abs(a[0] - b[0]) <= tolerance
            and abs(a[1] - b[1]) <= tolerance
            and abs(a[2] - b[2]) <= tolerance
        )

    visited = bytearray(width * height)
    bg_colors = [pixels[x, y][:3] for (x, y) in seeds]

    # Iterative flood-fill (a recursive version blows the stack on 1024px).
    stack = list(seeds)
    while stack:
        x, y = stack.pop()
        if x < 0 or x >= width or y < 0 or y >= height:
            continue
        idx = y * width + x
        if visited[idx]:
            continue
        rgba = pixels[x, y]
        rgb = rgba[:3]
        if not any(color_close(rgb, bg) for bg in bg_colors):
            continue
        visited[idx] = 1
        pixels[x, y] = (rgb[0], rgb[1], rgb[2], 0)
        stack.append((x + 1, y))
        stack.append((x - 1, y))
        stack.append((x, y + 1))
        stack.append((x, y - 1))

    out = io.BytesIO()
    img.save(out, format="PNG")
    return out.getvalue()


def _remove_background_rembg(png_bytes: bytes) -> bytes:
    """Run the `rembg` library (ONNX U2Net) over the input PNG and return
    a new RGBA PNG with the background alpha-cleared. Handles arbitrary
    backgrounds (gradients, busy textures, icon-touches-edge).

    Returns the original bytes on failure (with a logged warning) so the
    pipeline still produces an asset rather than crashing.
    """
    try:
        from rembg import remove  # type: ignore
    except ImportError:
        logger.warning(
            "rembg background removal requires the 'rembg' package. "
            "Install with: pip install rembg onnxruntime  "
            "(first call will download a ~170MB model)."
        )
        return png_bytes

    try:
        return remove(png_bytes)
    except Exception as e:
        logger.warning(f"rembg failed, returning original PNG: {e}")
        return png_bytes


def _remove_background(png_bytes: bytes, mode: str, tolerance: int) -> bytes:
    if mode == "none":
        return png_bytes
    if mode == "chroma":
        return _remove_background_chroma(png_bytes, tolerance=tolerance)
    if mode == "rembg":
        return _remove_background_rembg(png_bytes)
    logger.warning(f"unknown background_removal mode '{mode}', skipping")
    return png_bytes


# Style preset prompt prefixes — kept here so callers don't have to repeat
# the same boilerplate across every icon request. Designed for a medieval
# fantasy game (PRK) and chosen so all icons in a set look like they belong
# together.
_ICON_STYLE_PRESETS: Dict[str, str] = {
    "ui_icon_flat": (
        "Flat vector game UI icon, clean silhouette, bold outline, "
        "limited color palette, centered composition, no text, no watermark, "
        "isolated on transparent background, alpha channel, "
    ),
    "weapon_silhouette": (
        "Medieval fantasy weapon icon, three-quarter view, dark silhouette "
        "with subtle metallic highlights, painterly digital art, "
        "centered composition, isolated on transparent background, no text, "
    ),
    "item_painted": (
        "Hand-painted medieval RPG inventory icon, soft brush strokes, "
        "warm lighting, centered composition, isolated on transparent "
        "background, no text, no watermark, "
    ),
    "item_pixel": (
        "16-bit pixel art medieval RPG icon, clean pixel edges, limited "
        "palette, centered on transparent background, no text, "
    ),
    "ability_glyph": (
        "Glowing magical glyph icon for a fantasy game ability, radial "
        "symmetry, deep saturated color with bright highlights, centered, "
        "isolated on dark transparent background, no text, "
    ),
    "faction_emblem": (
        "Heraldic faction emblem for a medieval fantasy game, symmetrical "
        "design, bold contrasting colors, on a shield-like backing, centered "
        "on transparent background, no text, "
    ),
}


def _build_icon_prompt(
    prompt: str,
    style_preset: Optional[str],
    transparent_bg: bool,
) -> str:
    parts: List[str] = []
    if style_preset:
        prefix = _ICON_STYLE_PRESETS.get(style_preset)
        if prefix:
            parts.append(prefix)
        else:
            # Unknown preset — surface as a free-form prefix so user-supplied
            # values can still steer the prompt without us silently ignoring it.
            parts.append(f"{style_preset}, ")
    parts.append(prompt.strip())
    if transparent_bg and "transparent" not in prompt.lower():
        parts.append(
            ", isolated on transparent background, alpha channel, no background"
        )
    return "".join(parts)


@mcp.tool()
def generate_icon_asset(
    prompt: str,
    asset_name: str,
    destination_path: str = "/Game/UI/Icons/",
    style_preset: Optional[str] = "ui_icon_flat",
    transparent_bg: bool = True,
    aspect_ratio: str = "1:1",
    model: str = "imagen-4.0-generate-001",
    dry_run: bool = False,
    api_key_env: str = "GOOGLE_AI_API_KEY",
    timeout_seconds: int = 60,
    background_removal: str = "chroma",
    chroma_tolerance: int = 18,
) -> Dict[str, Any]:
    """Generate an icon via Google AI Studio (Imagen) and import it as a UTexture2D.

    Calls Imagen with the constructed prompt, decodes the returned PNG, then
    asks the editor to import it at destination_path/asset_name with UI-icon
    settings (UI texture group, sRGB, UserInterface2D compression, no mips,
    never streamed). Each call costs roughly $0.03-$0.04 — use dry_run to
    iterate on prompt wording cheaply.

    Args:
        prompt: Free-form description of the icon. Style preset is prepended.
        asset_name: Output asset name (no extension), e.g. "T_Icon_IronSword".
        destination_path: Content Browser folder, e.g. "/Game/UI/Icons/".
        style_preset: One of {ui_icon_flat, weapon_silhouette, item_painted,
            item_pixel, ability_glyph, faction_emblem}, or any free-form
            string (used as a literal prefix), or None to skip.
        transparent_bg: When true, append a transparent-bg hint to the prompt
            unless the caller already mentions "transparent".
        aspect_ratio: Imagen aspect ratio, e.g. "1:1", "3:4", "16:9".
        model: Imagen model id. Default is imagen-4.0-generate-001.
        dry_run: When true, return the constructed prompt and intended asset
            path *without* calling the API or touching the editor.
        api_key_env: Env var holding the Google AI Studio API key.
        timeout_seconds: HTTP timeout for the Imagen request.
        background_removal: Post-process the PNG before import. One of:
            "none"   — leave Imagen output untouched.
            "chroma" — flood-fill from corners, alpha-out background-colored
                pixels (default; requires Pillow). Best for Imagen's typical
                solid white/grey/black backgrounds. Fails when the icon
                touches the canvas edge or the bg has busy texture.
            "rembg"  — run the rembg ONNX U2Net model. Handles arbitrary
                backgrounds. Requires `pip install rembg onnxruntime`.
                Adds 1-3s per call; first call downloads a ~170MB model.
        chroma_tolerance: Per-channel RGB tolerance (0-255) for the chroma
            background-fill, when background_removal == "chroma". Higher
            values eat more of the icon edges; lower values leave bg
            speckles. Default 18 works well for clean Imagen backdrops.

    Returns:
        On dry_run: {success, dry_run: True, full_prompt, asset_path, model}.
        On success: {success: True, asset_path, package_name, size_x, size_y,
                     full_prompt, model}.
        On error:   {success: False, message}.
    """
    if background_removal not in _BG_REMOVAL_MODES:
        return {
            "success": False,
            "message": (
                f"background_removal must be one of {sorted(_BG_REMOVAL_MODES)}, "
                f"got '{background_removal}'."
            ),
        }

    full_prompt = _build_icon_prompt(prompt, style_preset, transparent_bg)
    intended_asset_path = f"{destination_path.rstrip('/')}/{asset_name}.{asset_name}"

    if dry_run:
        return {
            "success": True,
            "dry_run": True,
            "full_prompt": full_prompt,
            "asset_path": intended_asset_path,
            "model": model,
            "background_removal": background_removal,
        }

    api_key = os.environ.get(api_key_env)
    if not api_key:
        return {
            "success": False,
            "message": (
                f"Missing API key. Set the {api_key_env} environment variable "
                f"to your Google AI Studio key. Get one at "
                f"https://aistudio.google.com/apikey . The MCP server reads "
                f"env at process start, so restart the editor after setting it."
            ),
        }

    # Imagen REST endpoint (Generative Language API). Uses :predict for the
    # imagen-* family, which returns base64-encoded PNGs in predictions[].
    endpoint = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"{model}:predict"
    )
    body = {
        "instances": [{"prompt": full_prompt}],
        "parameters": {
            "sampleCount": 1,
            "aspectRatio": aspect_ratio,
            "personGeneration": "dont_allow",
        },
    }

    try:
        import requests  # listed in pyproject deps
    except ImportError:
        return {"success": False, "message": "Python 'requests' package is not installed."}

    try:
        resp = requests.post(
            endpoint,
            headers={
                "x-goog-api-key": api_key,
                "Content-Type": "application/json",
            },
            json=body,
            timeout=timeout_seconds,
        )
    except Exception as e:
        logger.error(f"generate_icon_asset HTTP error: {e}")
        return {"success": False, "message": f"Imagen request failed: {e}"}

    if resp.status_code != 200:
        # Surface API error body verbatim (truncated) — Imagen returns
        # actionable messages for safety filters, quota, bad model id, etc.
        snippet = resp.text[:500] if resp.text else "(empty)"
        return {
            "success": False,
            "message": f"Imagen returned HTTP {resp.status_code}: {snippet}",
        }

    try:
        payload = resp.json()
    except Exception as e:
        return {"success": False, "message": f"Imagen returned non-JSON: {e}"}

    predictions = payload.get("predictions") or []
    if not predictions:
        return {
            "success": False,
            "message": (
                "Imagen returned no predictions. This usually means the prompt "
                f"was filtered by safety. Full payload: {json.dumps(payload)[:400]}"
            ),
        }

    b64 = predictions[0].get("bytesBase64Encoded")
    if not b64:
        return {
            "success": False,
            "message": f"Imagen prediction missing image data: {json.dumps(predictions[0])[:300]}",
        }

    try:
        png_bytes = base64.b64decode(b64)
    except Exception as e:
        return {"success": False, "message": f"Failed to decode Imagen base64 PNG: {e}"}

    bg_removal_applied = background_removal
    if background_removal != "none":
        try:
            png_bytes = _remove_background(png_bytes, background_removal, chroma_tolerance)
        except Exception as e:
            # Don't fail the whole pipeline — surface the warning and import
            # the un-cleaned PNG. The texture is still usable; the user can
            # re-run with a different mode.
            logger.warning(f"background removal '{background_removal}' raised: {e}")
            bg_removal_applied = f"{background_removal} (failed: {e})"

    # Write to a temp PNG and let UE import it. Keep the temp file path stable
    # so we can pass delete_source_after_import and not leak files on /tmp.
    tmp_fd, tmp_path = tempfile.mkstemp(prefix=f"icon_{asset_name}_", suffix=".png")
    try:
        with os.fdopen(tmp_fd, "wb") as f:
            f.write(png_bytes)
    except Exception as e:
        try:
            os.close(tmp_fd)
        except Exception:
            pass
        return {"success": False, "message": f"Failed to write temp PNG: {e}"}

    unreal = get_unreal_connection()
    if not unreal:
        try:
            os.unlink(tmp_path)
        except Exception:
            pass
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    try:
        response = unreal.send_command("import_texture_for_icon", {
            "source_file": tmp_path,
            "destination_path": destination_path,
            "asset_name": asset_name,
            "replace_existing": True,
            "srgb": True,
            "no_mips": True,
            "never_stream": True,
            "compression_settings": "UserInterface2D",
            "delete_source_after_import": True,
        })
    except Exception as e:
        logger.error(f"generate_icon_asset import error: {e}")
        try:
            os.unlink(tmp_path)
        except Exception:
            pass
        return {"success": False, "message": f"Texture import failed: {e}"}

    if not response:
        return {"success": False, "message": "No response from Unreal during texture import."}

    # Editor side is responsible for deleting the temp file when
    # delete_source_after_import=True; clean up here too as a safety net
    # in case the import command failed before deletion.
    if os.path.exists(tmp_path):
        try:
            os.unlink(tmp_path)
        except Exception:
            pass

    response.setdefault("full_prompt", full_prompt)
    response.setdefault("model", model)
    response.setdefault("background_removal", bg_removal_applied)
    return response


# ============================================================================
# World Settings & Project Game Mode
# ============================================================================
#
# These wrappers route through run_python — no C++ command handler. They cover
# the two settings surfaces that have no dedicated tool:
#   - AWorldSettings on the currently-loaded editor level (per-map game mode,
#     KillZ, world-to-meters, time dilation, etc.)
#   - UGameMapsSettings (project-wide DefaultGameMode written to DefaultEngine.ini)
#
# Class-typed properties accept a class path string. Blueprint paths must end
# in "_C" (e.g. "/Game/AI/BP_PRKGameMode.BP_PRKGameMode_C"); native classes
# use "/Script/Module.ClassName".


def _py_literal(v: Any) -> str:
    """Render a JSON-ish Python value as a literal embeddable in source."""
    if v is None:
        return "None"
    if isinstance(v, bool):
        return "True" if v else "False"
    if isinstance(v, (int, float, str)):
        return repr(v)
    return repr(v)


# WorldSettings property names whose values are TSubclassOf<...> — must be
# resolved to a UClass before SetEditorProperty, not passed as a raw string.
_WORLDSETTINGS_CLASS_PROPS = {
    "default_game_mode",
    "game_network_manager_class",
}


@mcp.tool()
def set_world_settings(
    property_name: str,
    value: Any,
    level_path: str = "",
    save: bool = True,
) -> Dict[str, Any]:
    """Set a property on the editor world's ``AWorldSettings``.

    Targets the currently-loaded editor level by default. Pass ``level_path``
    (e.g. ``"/Game/Maps/MyLevel"``) to load a specific level first; the level
    is left open after the change.

    Common properties:
      - ``default_game_mode`` (TSubclassOf<AGameModeBase>): per-level GameMode
        override. Value is a class path string ending in ``"_C"`` for
        Blueprint classes (e.g. ``"/Game/AI/BP_PRKGameMode.BP_PRKGameMode_C"``)
        or a native class path (``"/Script/PRK.PRKGameMode"``). Pass ``None``,
        ``""`` or ``"None"`` to clear (falls back to project-wide default).
      - ``kill_z`` (float)
      - ``world_to_meters`` (float)
      - ``time_dilation`` (float)
      - ``bEnableWorldBoundsChecks`` / ``b_enable_world_bounds_checks`` (bool)

    Args:
        property_name: WorldSettings UPROPERTY field name (Python-style snake_case).
        value: Primitive (str/int/float/bool/None). For class-typed properties
            (see ``_WORLDSETTINGS_CLASS_PROPS``) pass a class path string.
        level_path: Optional ``/Game/...`` map asset. Empty = current level.
        save: Save the level after mutation (default True).

    Returns:
        ``{success, stdout, stderr, log, ...}`` from run_python.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    is_class_prop = property_name in _WORLDSETTINGS_CLASS_PROPS
    code = f"""
import unreal

prop_name = {_py_literal(property_name)}
new_value = {_py_literal(value)}
level_path = {_py_literal(level_path)}
do_save = {"True" if save else "False"}
is_class_prop = {"True" if is_class_prop else "False"}

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if level_path:
    if not les.load_level(level_path):
        raise RuntimeError("Failed to load level: " + level_path)

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
if world is None:
    raise RuntimeError("No editor world loaded")
ws = world.get_world_settings()
if ws is None:
    raise RuntimeError("World has no WorldSettings actor")

if is_class_prop:
    if new_value in (None, "", "None"):
        ws.set_editor_property(prop_name, None)
        resolved = "None (cleared)"
    else:
        obj = unreal.load_object(None, new_value)
        if obj is None:
            obj = unreal.load_class(None, new_value)
        if isinstance(obj, unreal.Blueprint):
            obj = obj.generated_class()
        if not isinstance(obj, unreal.Class):
            raise RuntimeError("Could not resolve class path: " + str(new_value))
        ws.set_editor_property(prop_name, obj)
        resolved = obj.get_path_name()
    print("WS." + prop_name + " <- " + resolved)
else:
    ws.set_editor_property(prop_name, new_value)
    print("WS." + prop_name + " <- " + repr(new_value))

if do_save:
    if not les.save_current_level():
        raise RuntimeError("save_current_level returned False")
    print("Saved level: " + world.get_path_name())
"""
    try:
        response = unreal.send_command("run_python", {
            "code": code,
            "mode": "ExecuteFile",
            "scope": "Private",
            "unattended": True,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_world_settings error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_world_settings(
    property_names: Optional[List[str]] = None,
    level_path: str = "",
) -> Dict[str, Any]:
    """Read selected properties from the editor world's ``AWorldSettings``.

    Useful for verifying state before/after ``set_world_settings``. Returns
    the values JSON-encoded inside the ``stdout`` of the underlying
    run_python call (look for the ``WS_READ:`` line) and structured under
    ``result`` when the editor side surfaces it.

    Args:
        property_names: List of UPROPERTY names to read (Python snake_case).
            Defaults to ``["default_game_mode", "kill_z", "world_to_meters",
            "time_dilation"]``.
        level_path: Optional map to load first. Empty = current level.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    if not property_names:
        property_names = [
            "default_game_mode",
            "kill_z",
            "world_to_meters",
            "time_dilation",
        ]

    code = f"""
import unreal, json

prop_names = {_py_literal(property_names)}
level_path = {_py_literal(level_path)}

if level_path:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(level_path):
        raise RuntimeError("Failed to load level: " + level_path)

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
ws = world.get_world_settings()

out = {{}}
for name in prop_names:
    try:
        v = ws.get_editor_property(name)
    except Exception as e:
        out[name] = {{"_error": str(e)}}
        continue
    if isinstance(v, unreal.Class):
        out[name] = v.get_path_name()
    elif v is None:
        out[name] = None
    else:
        try:
            json.dumps(v)
            out[name] = v
        except Exception:
            out[name] = repr(v)

print("WS_READ:" + json.dumps({{"world": world.get_path_name(), "values": out}}))
"""
    try:
        response = unreal.send_command("run_python", {
            "code": code,
            "mode": "ExecuteFile",
            "scope": "Private",
            "unattended": True,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_world_settings error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_project_game_mode(
    game_mode_class_path: Optional[str],
    map_overrides: Optional[Dict[str, Optional[str]]] = None,
    save: bool = True,
) -> Dict[str, Any]:
    """Set the project-wide DefaultGameMode in ``UGameMapsSettings``.

    Mutates the CDO and (when ``save=True``) calls ``save_config()`` which
    writes the ``[/Script/EngineSettings.GameMapsSettings]`` section of
    ``Config/DefaultEngine.ini``. Effect persists across editor restarts.

    Args:
        game_mode_class_path: Class path for ``GlobalDefaultGameMode``.
            Blueprint: ``"/Game/AI/BP_PRKGameMode.BP_PRKGameMode_C"`` (note
            the trailing ``_C``). Native: ``"/Script/PRK.PRKGameMode"``.
            Pass ``None`` or ``""`` to clear (falls back to engine default).
        map_overrides: Optional ``{map_path: game_mode_class_path_or_None}``
            entries written to ``GameModeMapPrefixes`` / ``GameModeClassAliases``.
            NOTE: per-map override is normally set on the level itself
            (``set_world_settings("default_game_mode", ...)``); this dict is
            only for the ini-level alias mechanism and is rarely needed.
        save: Persist to ``DefaultEngine.ini`` via ``save_config()``.

    Returns:
        ``{success, stdout, stderr, log, ...}`` from run_python.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    code = f"""
import unreal

class_path = {_py_literal(game_mode_class_path)}
do_save = {"True" if save else "False"}

gms = unreal.get_default_object(unreal.GameMapsSettings)
if gms is None:
    raise RuntimeError("Could not get UGameMapsSettings CDO")

if class_path in (None, "", "None"):
    try:
        gms.set_editor_property("global_default_game_mode", unreal.SoftClassPath(""))
    except Exception:
        gms.set_editor_property("global_default_game_mode", "")
    print("GMS.global_default_game_mode <- (cleared)")
else:
    try:
        gms.set_editor_property("global_default_game_mode", unreal.SoftClassPath(class_path))
        print("GMS.global_default_game_mode <- " + class_path + " (SoftClassPath)")
    except Exception as e:
        gms.set_editor_property("global_default_game_mode", class_path)
        print("GMS.global_default_game_mode <- " + class_path + " (string fallback: " + str(e) + ")")

if do_save:
    gms.save_config()
    print("Saved DefaultEngine.ini ([/Script/EngineSettings.GameMapsSettings])")
"""
    try:
        response = unreal.send_command("run_python", {
            "code": code,
            "mode": "ExecuteFile",
            "scope": "Private",
            "unattended": True,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_project_game_mode error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Enum + GameplayTag authoring
# ============================================================================

@mcp.tool()
def create_enum(
    name: str,
    package_path: str = "/Game/Data/",
    entries: Optional[List[Any]] = None,
    save: bool = False,
) -> Dict[str, Any]:
    """Create a UUserDefinedEnum asset in the Content Browser.

    Args:
        name: Asset name (e.g., "E_DamageType").
        package_path: Folder path (default "/Game/Data/").
        entries: Optional initial enumerators. Each may be a plain string (used
            as display name) or {"name", "display_name", "description"} dict.
            UE 5.7's UUserDefinedEnum API only exposes display-name + tooltip
            mutation; internal enumerator names stay auto-generated and are
            never user-facing in editor dropdowns.
        save: If true, save the asset to disk after creation.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"name": name, "package_path": package_path}
        if entries: params["entries"] = entries
        if save: params["save"] = True
        response = unreal.send_command("create_enum", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_enum error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def set_enum_entries(
    asset_path: str,
    entries: List[Any],
    save: bool = True,
) -> Dict[str, Any]:
    """Replace every enumerator on an existing UUserDefinedEnum.

    All existing entries are removed (Pre/PostEdit notifications fire so dependent
    Blueprint pins remain valid) and the supplied list is reapplied in order.

    Args:
        asset_path: Full path to the UUserDefinedEnum.
        entries: List of strings or {"name","display_name","description"} dicts.
        save: Save to disk after editing (default True).
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("set_enum_entries", {
            "asset_path": asset_path,
            "entries": entries,
            "save": save,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"set_enum_entries error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def read_enum(asset_path: str) -> Dict[str, Any]:
    """Read enumerators from a UEnum or UUserDefinedEnum asset.

    Returns each entry's internal name, display name (where present), and
    numeric value. Excludes the synthesized _MAX sentinel.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("read_enum", {"asset_path": asset_path})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"read_enum error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def create_gameplay_tag(
    tag: str,
    comment: str = "",
    source: str = "DefaultGameplayTags.ini",
) -> Dict[str, Any]:
    """Author a new gameplay tag via the editor's AddNewGameplayTagToINI path.

    Writes to the named tag-source ini, registers the tag with
    UGameplayTagsManager, and broadcasts change events to listeners. Returns
    success=False if the tag already exists or the source is missing — there
    is no "rename" or "update comment" path through this command; callers must
    edit the ini file directly for those cases.

    Args:
        tag: Tag string with dot-delimited segments (e.g., "Combat.Hit.Light").
        comment: Optional explanation for the tag editor UI.
        source: Tag source ini filename (default "DefaultGameplayTags.ini").
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("create_gameplay_tag", {
            "tag": tag,
            "comment": comment,
            "source": source,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"create_gameplay_tag error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def list_gameplay_tags(
    filter: Optional[str] = None,
    limit: int = 500,
) -> Dict[str, Any]:
    """List gameplay tags registered with UGameplayTagsManager.

    Args:
        filter: Optional case-insensitive substring filter (e.g., "Combat.").
        limit: Maximum number of tags to return.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"limit": limit}
        if filter: params["filter"] = filter
        response = unreal.send_command("list_gameplay_tags", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"list_gameplay_tags error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Blueprint node T3D round-trip
# ============================================================================

@mcp.tool()
def export_blueprint_nodes_t3d(
    blueprint_name: str,
    function_name: Optional[str] = None,
    node_ids: Optional[List[str]] = None,
) -> Dict[str, Any]:
    """Export Blueprint graph nodes to the editor's T3D copy-paste text format.

    Output is byte-identical to what UE produces when the user Ctrl+C's a
    selection of nodes, so the returned text can be pasted directly into the
    editor or fed to import_blueprint_nodes_t3d on another Blueprint / graph.

    Args:
        blueprint_name: Blueprint asset to read from.
        function_name: Optional function graph; if omitted, the event graph
            (first ubergraph page) is used.
        node_ids: Optional list of node GUIDs to filter. If omitted, exports
            every node on the resolved graph.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"blueprint_name": blueprint_name}
        if function_name: params["function_name"] = function_name
        if node_ids: params["node_ids"] = node_ids
        response = unreal.send_command("export_nodes_t3d", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"export_blueprint_nodes_t3d error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def import_blueprint_nodes_t3d(
    blueprint_name: str,
    t3d: str,
    function_name: Optional[str] = None,
    offset_x: float = 0.0,
    offset_y: float = 0.0,
) -> Dict[str, Any]:
    """Paste T3D node text into a Blueprint graph.

    Round-trips with export_blueprint_nodes_t3d. New GUIDs are assigned to the
    pasted nodes so subsequent connect_nodes / set_node_property calls can
    reference them via the returned imported_node_ids list.

    Args:
        blueprint_name: Target Blueprint.
        t3d: Exported T3D text (from export_blueprint_nodes_t3d or the editor's
            clipboard).
        function_name: Optional function graph; defaults to the event graph.
        offset_x, offset_y: Uniform translation applied to all pasted nodes so
            clusters can be placed without overlapping existing content.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {"blueprint_name": blueprint_name, "t3d": t3d}
        if function_name: params["function_name"] = function_name
        if offset_x: params["offset_x"] = offset_x
        if offset_y: params["offset_y"] = offset_y
        response = unreal.send_command("import_nodes_t3d", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"import_blueprint_nodes_t3d error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Asset full-text search
# ============================================================================

@mcp.tool()
def search_assets_fts(
    query: str,
    directory: str = "/Game",
    class_filter: Optional[str] = None,
    limit: int = 50,
    case_sensitive: bool = False,
    names_only: bool = False,
    snippet_radius: int = 48,
) -> Dict[str, Any]:
    """Substring search across asset names and AssetRegistry tag values.

    The AssetRegistry tags are the same set the Content Browser indexes —
    AssetRegistrySearchable UPROPERTY values, display names, descriptions, tag
    container snapshots, etc. — so matches surface without loading the asset
    packages. For deeper searches that require loading (e.g., scanning every
    K2 node literal in a Blueprint graph), pair this with
    read_data_asset / read_blueprint_content on returned paths.

    Args:
        query: Substring to find.
        directory: Content folder, recursive (default "/Game").
        class_filter: Restrict to a single class (short name or script path).
        limit: Max number of asset matches to return.
        case_sensitive: Default false.
        names_only: Match only against asset_name / package_name; skip tag
            scanning (use when you only want pathy matches).
        snippet_radius: Characters of context shown around each tag-value hit.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        params: Dict[str, Any] = {
            "query": query,
            "directory": directory,
            "limit": limit,
            "case_sensitive": case_sensitive,
            "names_only": names_only,
            "snippet_radius": snippet_radius,
        }
        if class_filter: params["class_filter"] = class_filter
        response = unreal.send_command("search_assets_fts", params)
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"search_assets_fts error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def wait_for_asset_compile() -> Dict[str, Any]:
    """Block until all async asset compilation is finished, then report the real remaining count.

    Asset edits (mesh build, texture, material, Nanite) compile ASYNC — the call that kicks them
    returns before they finish, so a follow-up read can see stale state. Call this after a batch
    of edits (e.g. generate_convex_collision, imports) and before reading results or screenshotting.

    Returns: remaining_before, remaining (0 when fully drained), complete.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("wait_for_asset_compile", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"wait_for_asset_compile error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def wait_for_shaders() -> Dict[str, Any]:
    """Block until all shader compilation is finished, then report the real remaining job count.

    Material/shader edits compile async; a screenshot or material-stats read taken before they
    finish shows the default/incomplete shader. Call this after material edits and before capture.

    Returns: remaining_before, remaining (0 when drained), complete.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("wait_for_shaders", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"wait_for_shaders error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_navmesh_build_status() -> Dict[str, Any]:
    """Report editor-world navmesh build status (non-blocking).

    Navmesh build is async and fire-and-forget — a rebuild call returns before tiles are built.
    Poll this until complete instead of assuming a rebuild finished. NOTE: under invoker-only
    dynamic generation, "complete" means the build QUEUE is drained, not that the whole world is
    covered — confirm actual coverage in PIE.

    Returns: has_navigation_system, in_progress, remaining_tasks, running_tasks, complete.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("get_navmesh_build_status", {})
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"get_navmesh_build_status error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def save_asset_checked(asset_path: str, use_source_control: bool = True) -> Dict[str, Any]:
    """Save an asset after guaranteeing it is writable — avoids the read-only-save FATAL assert.

    A read-only .uasset (Perforce "not checked out", or a read-only file attribute) makes the
    save path hit a fatal assert that takes down the editor, not a soft failure. This checks the
    file out under source control (best-effort) AND clears the read-only attribute before saving.
    Use it instead of save_data_asset / SaveLoadedAsset whenever the asset may be under P4 or
    read-only (landscape masters, vendored content, freshly synced files).

    Args:
        asset_path: Full asset path to save.
        use_source_control: Attempt a source-control checkout first (default True; harmless no-op
            when SCC is disabled). The read-only attribute is cleared regardless.

    Returns: saved, was_read_only, source_control_checked_out, filename.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("save_asset_checked", {
            "asset_path": asset_path,
            "use_source_control": use_source_control,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"save_asset_checked error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# Batch actor placement
# ============================================================================

def _normalize_batch_transform(value: Any, field: str, index: int) -> List[float]:
    """Convert the public transform shape to the native command's three-number array."""
    axes = ("pitch", "yaw", "roll") if field == "rotation" else ("x", "y", "z")
    error_prefix = f"actors[{index}].{field}"
    if isinstance(value, dict):
        components = {}
        for key, component in value.items():
            if not isinstance(key, str) or key.lower() not in axes or key.lower() in components:
                raise ValueError(f"{error_prefix}: expected exactly {', '.join(axes)} (case-insensitive)")
            components[key.lower()] = component
        if set(components) != set(axes):
            raise ValueError(f"{error_prefix}: expected exactly {', '.join(axes)} (case-insensitive)")
        values = [components[axis] for axis in axes]
    elif isinstance(value, (list, tuple)) and len(value) == 3:
        values = value
    else:
        raise ValueError(f"{error_prefix}: expected a three-number array or component dictionary")

    normalized = []
    for axis, component in zip(axes, values):
        if isinstance(component, bool) or not isinstance(component, (int, float)):
            raise ValueError(f"{error_prefix}.{axis}: expected a finite number")
        try:
            number = float(component)
        except OverflowError:
            raise ValueError(f"{error_prefix}.{axis}: expected a finite number") from None
        if not math.isfinite(number):
            raise ValueError(f"{error_prefix}.{axis}: expected a finite number")
        # The native JSON readers cast components to float before creating the
        # transform. Reject overflow and scales which that cast would collapse.
        try:
            native_number = struct.unpack("f", struct.pack("f", number))[0]
        except OverflowError:
            raise ValueError(f"{error_prefix}.{axis}: must fit a finite Unreal float") from None
        if not math.isfinite(native_number):
            raise ValueError(f"{error_prefix}.{axis}: must fit a finite Unreal float")
        if field == "scale" and native_number == 0:
            raise ValueError(f"{error_prefix}.{axis}: scale components must be nonzero")
        normalized.append(number)
    return normalized


def _normalize_batch_actor_entries(actors: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """Validate the whole batch before any actor can be spawned; leave caller input untouched."""
    if not isinstance(actors, list):
        raise ValueError("actors: expected a list of entry dictionaries")
    normalized = []
    for index, entry in enumerate(actors):
        if not isinstance(entry, dict):
            raise ValueError(f"actors[{index}]: expected an entry dictionary")
        entry = dict(entry)
        for field in ("location", "rotation", "scale"):
            if field in entry:
                entry[field] = _normalize_batch_transform(entry[field], field, index)
        normalized.append(entry)
    return normalized


@mcp.tool()
def place_actors_batch(
    actors: List[Dict[str, Any]],
    stop_on_error: bool = True,
    select_spawned: bool = False,
) -> Dict[str, Any]:
    """Spawn a batch of actors with per-instance class, transform, and properties.

    Each entry in `actors` is a dict:
        actor_class:  Class to spawn. Short name ("StaticMeshActor", "MyBP"),
                      "U"-prefixed name, or "/Script/Module.Class" path. The
                      keyword "type" is accepted as a synonym (spawn_actor compat).
        location:     {"x": number,"y": number,"z": number} or [x,y,z] (default origin).
        rotation:     {"pitch": number,"yaw": number,"roll": number} or [pitch,yaw,roll]
                      (default identity). Component dictionary keys are case-insensitive.
        scale:        {"x": number,"y": number,"z": number} or [x,y,z] (default 1,1,1).
                      All transform components must be finite numbers; scale cannot contain zero.
        name:         Optional desired UObject actor name, not its Outliner label. Collisions auto-uniquify
                      rather than failing the whole batch.
        static_mesh:  Asset path applied when the spawned actor is an
                      AStaticMeshActor.
        properties:   Per-instance UPROPERTY map, same shape update_data_asset
                      accepts. Each property edit fires PreEditChange /
                      PostEditChangeProperty so component/CDO derived state
                      stays consistent.

    Args:
        actors: List of entry dicts as described above.
        stop_on_error: If true (default), the first failure aborts; remaining
            entries are skipped and reported as failures. If false, the batch
            keeps spawning and aggregates errors.
        select_spawned: If true, the editor selection is set to the spawned
            actors after the batch finishes (useful for follow-up Outliner ops).

    Invalid transforms and Errant Landscape brush classes are refused for the whole batch
    before spawning (use errant_stamp_spawn for brushes).
    """
    try:
        actors = _normalize_batch_actor_entries(actors)
    except ValueError as e:
        return {"success": False, "refused": True, "message": str(e)}
    refused = _errant_brush_guard([(e or {}).get("actor_class") or (e or {}).get("type") for e in (actors or [])
                                   if isinstance(e, dict)], [], "place_actors_batch")
    if refused:
        return refused
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("place_actors_batch", {
            "actors": actors,
            "stop_on_error": stop_on_error,
            "select_spawned": select_spawned,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"place_actors_batch error: {e}")
        return {"success": False, "message": str(e)}


# ============================================================================
# YAML flow engine
# ============================================================================
#
# Multi-step orchestrator with structured logging and best-effort rollback.
# Steps are dispatched against this same module's @mcp.tool() functions, so any
# tool already exposed to the LLM is also callable from a flow.
#
# Schema:
#   name: descriptive label (optional)
#   description: free text (optional)
#   on_error: rollback | continue | abort  (default: rollback)
#   steps:
#     - name: step_id                       (required, used for ${steps.X.*} refs)
#       tool: <python_tool_name>            (e.g. create_data_asset)
#       args: { <kwargs for the tool> }
#       on_fail: rollback | continue | abort  (per-step override; falls back to on_error)
#
# Variable substitution:
#   String values in args matching the regex ${steps.<step_name>.<json_path>}
#   are replaced at execution time. JSON path is dot/bracket-delimited and
#   resolves against the prior step's response dict.
#
# Rollback model:
#   Each step records an "undo" action when its forward tool is one of the
#   known-reversible operations (create_/update_data_asset, create_blueprint,
#   create_widget_blueprint, create_enum). On failure with rollback policy,
#   the engine plays the undo log in reverse. Tools without a known undo are
#   marked irreversible and emit a warning when rollback is required.
#
# This is best-effort, not transactional. Engine state changes that happen
# inside a tool (asset registry events, listener callbacks) are NOT undone —
# only the asset itself is restored. Use git/Perforce for true rollback of
# source-controlled state.

_FLOW_VAR_RE = None  # compiled lazily

def _flow_var_pattern():
    global _FLOW_VAR_RE
    if _FLOW_VAR_RE is None:
        import re
        _FLOW_VAR_RE = re.compile(r"\$\{steps\.([A-Za-z0-9_]+)\.([^}]+)\}")
    return _FLOW_VAR_RE


def _flow_resolve_path(obj: Any, path: str) -> Any:
    """Resolve a dotted/bracketed path into a nested dict/list."""
    cur = obj
    for raw in path.replace("[", ".[").split("."):
        if not raw:
            continue
        if raw.startswith("["):
            try:
                idx = int(raw.strip("[]"))
                cur = cur[idx]
            except (ValueError, IndexError, TypeError):
                return None
        else:
            if isinstance(cur, dict):
                cur = cur.get(raw)
            else:
                return None
        if cur is None:
            return None
    return cur


def _flow_substitute(value: Any, step_outputs: Dict[str, Dict[str, Any]]) -> Any:
    """Walk an args tree replacing ${steps.X.path} tokens with prior outputs."""
    if isinstance(value, dict):
        return {k: _flow_substitute(v, step_outputs) for k, v in value.items()}
    if isinstance(value, list):
        return [_flow_substitute(v, step_outputs) for v in value]
    if isinstance(value, str):
        pattern = _flow_var_pattern()
        # Single-token strings resolve to the raw value (preserves dicts/lists);
        # interpolated strings stringify.
        match = pattern.fullmatch(value)
        if match:
            step_name, path = match.group(1), match.group(2)
            return _flow_resolve_path(step_outputs.get(step_name, {}), path)
        def _sub(m):
            step_name, path = m.group(1), m.group(2)
            resolved = _flow_resolve_path(step_outputs.get(step_name, {}), path)
            return "" if resolved is None else str(resolved)
        return pattern.sub(_sub, value)
    return value


def _flow_snapshot_and_undo(tool_name: str, args: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Decide what undo action (if any) reverses a forward step.

    Returns either {"tool": str, "args": dict} to call on rollback, or None for
    irreversible tools. update_data_asset snapshots the prior state synchronously
    so the rollback edit is precise rather than guesswork.
    """
    if tool_name == "create_data_asset":
        # Reverse via delete_data_asset — the asset didn't exist before this step.
        package = args.get("package_path", "/Game/Data/")
        if not package.endswith("/"):
            package += "/"
        asset_path = package + args["name"]
        return {"tool": "delete_data_asset", "args": {"asset_path": asset_path}}

    if tool_name in ("create_blueprint",):
        # No standard "blueprint name -> path" map — fall back to delete_blueprint
        # after the forward call records the path.
        return {"_post_record": True, "tool": "delete_blueprint", "args": {}}

    if tool_name == "create_widget_blueprint":
        return {"_post_record": True, "tool": "delete_widget_blueprint", "args": {}}

    if tool_name == "create_enum":
        package = args.get("package_path", "/Game/Data/")
        if not package.endswith("/"):
            package += "/"
        asset_path = package + args["name"]
        # delete_data_asset works for any UObject asset, including UUserDefinedEnum.
        return {"tool": "delete_data_asset", "args": {"asset_path": asset_path}}

    if tool_name == "update_data_asset":
        # Snapshot pre-edit state and restore exactly those properties on rollback.
        try:
            asset_path = args["asset_path"]
            properties = args.get("properties", {})
            before = read_data_asset(asset_path=asset_path)  # type: ignore[name-defined]
            if not before or not before.get("success"):
                return None
            before_props = before.get("properties", {})
            restore = {k: before_props.get(k) for k in properties.keys() if k in before_props}
            return {"tool": "update_data_asset", "args": {
                "asset_path": asset_path,
                "properties": restore,
                "save": True,
            }}
        except Exception:
            return None

    return None  # unknown tool — irreversible


def _flow_run_steps(flow: Dict[str, Any]) -> Dict[str, Any]:
    steps = flow.get("steps") or []
    if not isinstance(steps, list):
        return {"success": False, "message": "'steps' must be a list"}

    default_policy = (flow.get("on_error") or "rollback").lower()
    if default_policy not in ("rollback", "continue", "abort"):
        return {"success": False, "message": f"Invalid on_error: {default_policy}"}

    outputs: Dict[str, Dict[str, Any]] = {}
    undo_log: List[Dict[str, Any]] = []
    step_records: List[Dict[str, Any]] = []
    irreversible_used: List[str] = []

    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            return {"success": False, "message": f"Step {index} is not a mapping"}
        step_name = step.get("name") or f"step_{index}"
        tool_name = step.get("tool")
        raw_args = step.get("args") or {}
        if not tool_name:
            return {"success": False, "message": f"Step '{step_name}' missing 'tool'"}

        tool_fn = globals().get(tool_name)
        if not callable(tool_fn):
            return {"success": False, "message": f"Step '{step_name}': tool '{tool_name}' not found"}

        try:
            resolved_args = _flow_substitute(raw_args, outputs)
        except Exception as e:
            return {"success": False, "message": f"Step '{step_name}': variable substitution failed: {e}"}

        # Compute undo BEFORE the forward call so update_data_asset can snapshot.
        undo = _flow_snapshot_and_undo(tool_name, resolved_args)
        if undo is None and tool_name.startswith(("create_", "update_", "delete_", "set_", "add_", "remove_", "rename_")):
            irreversible_used.append(f"{step_name}:{tool_name}")

        # Forward step.
        try:
            result = tool_fn(**resolved_args) if isinstance(resolved_args, dict) else tool_fn(resolved_args)
        except TypeError as e:
            result = {"success": False, "message": f"TypeError calling {tool_name}: {e}"}
        except Exception as e:
            result = {"success": False, "message": f"Exception in {tool_name}: {e}"}

        if not isinstance(result, dict):
            result = {"success": True, "value": result}

        outputs[step_name] = result
        step_records.append({
            "step": step_name,
            "tool": tool_name,
            "success": bool(result.get("success", False)),
            "result": result,
        })

        # Post-record undo for tools whose result tells us the created path.
        if undo and undo.get("_post_record"):
            created_path = result.get("asset_path") or result.get("path")
            if created_path:
                undo_args = dict(undo.get("args") or {})
                undo_args["asset_path"] = created_path
                undo_log.append({"tool": undo["tool"], "args": undo_args, "for_step": step_name})
        elif undo:
            undo_log.append({"tool": undo["tool"], "args": undo["args"], "for_step": step_name})

        if not result.get("success", False):
            policy = (step.get("on_fail") or default_policy).lower()
            if policy == "continue":
                continue
            # rollback or abort
            rollback_results: List[Dict[str, Any]] = []
            if policy == "rollback":
                for undo_action in reversed(undo_log):
                    undo_fn = globals().get(undo_action["tool"])
                    if not callable(undo_fn):
                        rollback_results.append({"step": undo_action["for_step"], "skipped": True, "reason": "undo tool missing"})
                        continue
                    try:
                        ur = undo_fn(**undo_action["args"])
                    except Exception as e:
                        ur = {"success": False, "message": str(e)}
                    rollback_results.append({"step": undo_action["for_step"], "tool": undo_action["tool"], "result": ur})
            return {
                "success": False,
                "failed_step": step_name,
                "failed_tool": tool_name,
                "policy": policy,
                "steps": step_records,
                "rollback": rollback_results if policy == "rollback" else None,
                "irreversible_warnings": irreversible_used,
            }

    return {
        "success": True,
        "steps": step_records,
        "irreversible_warnings": irreversible_used,
    }


@mcp.tool()
def run_flow(flow_yaml: str) -> Dict[str, Any]:
    """Execute a multi-step YAML flow against this MCP server's tools.

    The YAML body describes a sequence of tool calls with variable substitution
    between steps and best-effort rollback on failure. Steps dispatch against
    every @mcp.tool() function exposed by this server, so any tool the LLM can
    call directly is also callable from a flow.

    Schema:
        name: optional label
        description: optional free text
        on_error: rollback | continue | abort   (default: rollback)
        steps:
          - name: my_step
            tool: create_data_asset
            args: { ... }
            on_fail: rollback | continue | abort  (per-step override)

    Variable substitution:
        String args matching ${steps.<step>.<json_path>} are replaced with the
        named step's response field at execution time. A single-token string
        resolves to the raw value (preserving dicts/lists); embedded tokens
        stringify.

    Rollback (best-effort, NOT transactional):
        - create_data_asset / create_enum → delete the created asset.
        - create_blueprint / create_widget_blueprint → delete the created BP.
        - update_data_asset → re-applies the pre-edit property snapshot.
        - Any other create_/update_/delete_/set_/add_/remove_/rename_ tool is
          flagged as irreversible — rollback skips it with a warning.

    Use git / Perforce when you need real transactional safety.

    Args:
        flow_yaml: YAML document body as a string.
    """
    try:
        import yaml  # PyYAML — required dependency of the flow engine only
    except ImportError:
        return {"success": False, "message": "PyYAML is not installed. Run `uv pip install pyyaml` in the UnrealMCP venv."}
    try:
        flow = yaml.safe_load(flow_yaml)
    except yaml.YAMLError as e:
        return {"success": False, "message": f"YAML parse error: {e}"}
    if not isinstance(flow, dict):
        return {"success": False, "message": "Top-level YAML must be a mapping"}
    return _flow_run_steps(flow)


@mcp.tool()
def run_flow_file(path: str) -> Dict[str, Any]:
    """Execute a YAML flow from a file path. See run_flow for the schema.

    Args:
        path: Absolute or relative filesystem path to a YAML flow file.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            body = f.read()
    except OSError as e:
        return {"success": False, "message": f"Failed to read flow file: {e}"}
    return run_flow(flow_yaml=body)


# ============================================================================
# Editor-Runtime Tools
# ============================================================================
#
# These tools wrap the *running* editor — console exec, PIE control, viewport,
# selection, asset references, Live Coding, in-editor automation runs, and
# log/screenshot capture. All route through run_python (no C++ handler needed).
#
# Pair this with the sibling `ue-toolchain-mcp` server, which handles the
# outside-the-editor toolchain (Build.bat, headless -ExecCmds, log/crash files).


def _editor_run_python(code: str, scope: str = "Private", statement: bool = False) -> Dict[str, Any]:
    """Dispatch a Python snippet to the running editor via run_python."""
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("run_python", {
            "code": "exec(" + repr(code) + ")" if statement else code,
            "mode": "ExecuteStatement" if statement else "ExecuteFile",
            "scope": scope,
            "unattended": True,
        })
        return response or {"success": False, "message": "No response from Unreal"}
    except Exception as e:
        logger.error(f"editor_run_python error: {e}")
        return {"success": False, "message": str(e)}


def _extract_json_marker(response: Dict[str, Any], marker: str = "__MCP_RESULT__") -> Optional[Dict[str, Any]]:
    """Pull a JSON payload printed between marker lines out of run_python stdout."""
    if not response or not isinstance(response, dict):
        return None
    stdout = response.get("stdout") or ""
    if not stdout and isinstance(response.get("log"), list):
        stdout = "\n".join(entry.get("text", "") for entry in response["log"] if entry.get("type") == "Info")
    if not stdout:
        # run_python responses nest the payload under "result"
        inner = response.get("result")
        if isinstance(inner, dict):
            stdout = inner.get("stdout") or ""
            if not stdout and isinstance(inner.get("log"), list):
                stdout = "\n".join(
                    entry.get("text", "") for entry in inner["log"] if entry.get("type") == "Info")
    start = stdout.find(f"<<<{marker}>>>")
    end = stdout.find(f"<<</{marker}>>>")
    if start == -1 or end == -1 or end < start:
        return None
    payload = stdout[start + len(marker) + 6:end].strip()
    try:
        return json.loads(payload)
    except json.JSONDecodeError as exc:
        # The editor printed the marker but the payload between the tags is not
        # valid JSON. Previously this returned None silently, so the caller saw an
        # empty {} and could not tell a genuine "no result" from a malformed one
        # (this backs 30+ tools). Log the diagnostic; keep returning None so the
        # caller's behavior is unchanged.
        logger.warning(
            "_extract_json_marker: marker '%s' found but payload is not valid JSON "
            "(%s). First 300 chars: %r",
            marker, exc, payload[:300],
        )
        return None


# Project rule: automation never runs in the user's live editor or PIE session,
# only in an isolated headless UnrealEditor-Cmd process.
_AUTOMATION_REFUSAL = (
    "Refused: automation tests never run in the live editor or PIE session. Use the "
    "ue-toolchain run_automation_tests_headless tool (isolated UnrealEditor-Cmd -NullRHI)."
)


def _is_automation_console_command(command: str) -> bool:
    """True if any '|'- or ';'-separated segment is an ``Automation ...`` command."""
    return any(seg.strip().lower().startswith("automation")
               for seg in command.replace(";", "|").split("|"))


@mcp.tool()
def exec_console_command(command: str, world_context: str = "editor") -> Dict[str, Any]:
    """Execute an Unreal console command in the running editor.

    Faster than wrapping every console command in a run_python call. Examples:
    ``stat unit``, ``r.ScreenPercentage 100``, ``LiveCoding.Compile``,
    ``ke * ToggleDebug``. ``Automation ...`` commands are refused — tests run
    headless only (ue-toolchain ``run_automation_tests_headless``).

    Args:
        command: The console command string (no leading slash).
        world_context: ``editor`` (default — uses the editor world) or ``pie``
            (uses the active PIE world if one exists, else falls back to editor).

    Returns:
        ``{success, command, executed_in}`` — console exec does not return a
        value; use ``tail_output_log`` to read effects.
    """
    if _is_automation_console_command(command):
        return {
            "success": False,
            "refused": True,
            "command": command,
            "message": _AUTOMATION_REFUSAL,
            "use_instead": ["run_automation_tests_headless (ue-toolchain)"],
        }
    code = f"""
import unreal, json
cmd = {_py_literal(command)}
ctx_pref = {_py_literal(world_context)}
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = None
if ctx_pref == "pie":
    try:
        world = ues.get_game_world() or ues.get_editor_world()
    except Exception:
        world = ues.get_editor_world()
else:
    world = ues.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, cmd)
print("<<<__MCP_RESULT__>>>")
print(json.dumps({{"executed_in": "pie" if (ctx_pref == "pie" and world is not None) else "editor"}}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": bool(response.get("success", False)),
        "command": command,
        "executed_in": marker.get("executed_in", "editor"),
        "raw": response,
    }


@mcp.tool()
def run_automation_tests_in_editor(
    test_filter: str,
    wait_seconds: float = 60.0,
    quiet_seconds: float = 3.0,
) -> Dict[str, Any]:
    """DISABLED — always refuses, with no editor round-trip.

    Project rule: automation never runs inside the user's interactive editor or PIE
    session. Tests run only in an isolated headless ``UnrealEditor-Cmd -NullRHI``
    process — use the ue-toolchain ``run_automation_tests_headless`` tool."""
    return {
        "success": False,
        "refused": True,
        "filter": test_filter,
        "message": _AUTOMATION_REFUSAL,
        "use_instead": ["run_automation_tests_headless (ue-toolchain)"],
    }


@mcp.tool()
def tail_output_log(
    max_lines: int = 200,
    severity_filter: str = "",
    contains: str = "",
    log_path: str = "",
) -> Dict[str, Any]:
    """Read the tail of the editor's on-disk output log.

    Works while the editor is running (UE keeps the file open in shared-read
    mode). For live-buffered (in-memory) tailing across many short calls, use
    the ``since_byte`` field returned here to seek on the next call — passed
    via ``contains`` filter is applied AFTER the byte window.

    Args:
        max_lines: Maximum lines to return from the tail (default 200).
        severity_filter: ``Error`` / ``Warning`` / ``Display`` / `""` (all).
        contains: Substring filter applied to each line.
        log_path: Override the log path. Default: ``<Project>/Saved/Logs/<Project>.log``.

    Returns:
        ``{success, log_path, total_bytes, returned_lines, lines: [...], next_since_byte}``.
    """
    code = f"""
import unreal, json, os
max_lines = int({_py_literal(int(max_lines))})
sev = {_py_literal(severity_filter)}
contains = {_py_literal(contains)}
override = {_py_literal(log_path)}
proj_dir = unreal.SystemLibrary.get_project_directory()
proj_name = unreal.SystemLibrary.get_game_name()
default_path = os.path.join(proj_dir, "Saved", "Logs", proj_name + ".log")
path = override if override else default_path
out = {{"log_path": path, "total_bytes": 0, "returned_lines": 0, "lines": [], "next_since_byte": 0}}
try:
    size = os.path.getsize(path)
    out["total_bytes"] = size
    # Read the last N KB to bound work; 256KB is plenty for 200 lines.
    window = min(size, 262144)
    with open(path, "rb") as f:
        f.seek(size - window)
        data = f.read(window)
    text = data.decode("utf-8", errors="replace")
    lines = text.splitlines()
    if sev:
        lines = [ln for ln in lines if (": " + sev + ":") in ln or ("[" + sev + "]") in ln]
    if contains:
        lines = [ln for ln in lines if contains in ln]
    lines = lines[-max_lines:]
    out["lines"] = lines
    out["returned_lines"] = len(lines)
    out["next_since_byte"] = size
except FileNotFoundError:
    out["error"] = "log file not found"
except Exception as e:
    out["error"] = str(e)
print("<<<__MCP_RESULT__>>>")
print(json.dumps(out))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    result = {"success": bool(response.get("success", False))}
    result.update(marker)
    return result


@mcp.tool()
def compile_with_live_coding() -> Dict[str, Any]:
    """Trigger a Live Coding compile of dirty C++ in the running editor.

    Equivalent to pressing Ctrl+Alt+F11. The editor must have Live Coding
    enabled (Editor Preferences → Live Coding) and a Visual Studio toolchain.

    The compile runs asynchronously. This tool returns once the command is
    dispatched; use ``tail_output_log(contains="LiveCoding")`` to check
    progress and the final ``Patch Complete`` / ``Patch Failed`` line.

    Returns:
        ``{success, dispatched}``.
    """
    code = """
import unreal, json
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "LiveCoding.Compile")
print("<<<__MCP_RESULT__>>>")
print(json.dumps({"dispatched": True}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": bool(response.get("success", False)),
        "dispatched": marker.get("dispatched", False),
        "raw": response,
    }


@mcp.tool()
def pie_start(simulate: bool = False) -> Dict[str, Any]:
    """Start a Play-In-Editor session.

    Uses the UE 5.7 LevelEditorSubsystem API:
      - simulate=False → EditorRequestBeginPlay (PIE with possession)
      - simulate=True  → EditorPlaySimulate     (no possession)

    Args:
        simulate: True for Simulate-In-Editor.

    Returns:
        ``{success, mode, was_already_active}``.
    """
    code = f"""
import unreal, json
simulate = {_py_literal(bool(simulate))}
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
was_active = bool(les.is_in_play_in_editor())
if was_active:
    les.editor_request_end_play()
if simulate:
    les.editor_play_simulate()
else:
    les.editor_request_begin_play()
print("<<<__MCP_RESULT__>>>")
print(json.dumps({{
    "mode": "simulate" if simulate else "pie",
    "was_already_active": was_active,
    "dispatched": True,
}}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": bool(response.get("success", False)) and marker.get("dispatched", False),
        "mode": marker.get("mode", ""),
        "was_already_active": marker.get("was_already_active", False),
        "raw": response,
    }


@mcp.tool()
def pie_stop() -> Dict[str, Any]:
    """Stop the active Play-In-Editor / Simulate session.

    Returns:
        ``{success, was_active}``.
    """
    code = """
import unreal, json
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
was_active = bool(les.is_in_play_in_editor())
if was_active:
    les.editor_request_end_play()
print("<<<__MCP_RESULT__>>>")
print(json.dumps({"was_active": was_active, "stopped": True}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": marker.get("stopped", False),
        "was_active": marker.get("was_active", False),
        "raw": response,
    }


@mcp.tool()
def pie_pause(paused: bool = True) -> Dict[str, Any]:
    """Pause or resume the active PIE session.

    Args:
        paused: True to pause (default), False to resume.

    Returns:
        ``{success, paused}``.
    """
    code = f"""
import unreal, json
want_pause = {_py_literal(bool(paused))}
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_game_world() if hasattr(ues, 'get_game_world') else None
if world is None:
    world = ues.get_editor_world()
cmd = "Pause" if want_pause else "Pause"  # Console "Pause" toggles. Better: use UGameplayStatics.
try:
    unreal.GameplayStatics.set_game_paused(world, want_pause)
    ok = True
except Exception:
    unreal.SystemLibrary.execute_console_command(world, "Pause")
    ok = True
print("<<<__MCP_RESULT__>>>")
print(json.dumps({{"paused": want_pause, "ok": ok}}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": marker.get("ok", False),
        "paused": marker.get("paused", paused),
        "raw": response,
    }


@mcp.tool()
def get_viewport_state() -> Dict[str, Any]:
    """Capture the editor viewport camera transform and selected actors.

    Returns:
        ``{success, camera: {location, rotation}, selected_actor_count,
        current_level, in_pie}``.
    """
    code = """
import unreal, json
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
loc, rot = ues.get_level_viewport_camera_info()
selected = eas.get_selected_level_actors() if eas else []
level_name = world.get_name() if world else ""
in_pie = False
try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if hasattr(les, 'is_in_play_in_editor'):
        in_pie = bool(les.is_in_play_in_editor())
except Exception:
    pass
out = {
    "camera": {
        "location": [loc.x, loc.y, loc.z],
        "rotation": [rot.pitch, rot.yaw, rot.roll],
    },
    "selected_actor_count": len(selected),
    "selected_actor_names": [a.get_actor_label() for a in selected[:50]],
    "current_level": level_name,
    "in_pie": in_pie,
}
print("<<<__MCP_RESULT__>>>")
print(json.dumps(out))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    result = {"success": bool(response.get("success", False))}
    result.update(marker)
    return result


@mcp.tool()
def capture_viewport(
    file_path: str = "",
    resolution_x: int = 1920,
    resolution_y: int = 1080,
    mask_enabled: bool = False,
    show_hud: bool = True,
) -> Dict[str, Any]:
    """Take a high-res screenshot of the active editor viewport.

    The file is written by the editor on the host filesystem. If file_path
    is empty, defaults to ``<Project>/Saved/Screenshots/MCP_<timestamp>.png``.

    Args:
        file_path: Absolute path for the PNG. Empty for default.
        resolution_x: Pixel width.
        resolution_y: Pixel height.
        mask_enabled: Render the object-id mask channel.
        show_hud: Include HUD/widgets.

    Returns:
        ``{success, file_path}``.
    """
    code = f"""
import unreal, json, os, time
override = {_py_literal(file_path)}
rx = int({_py_literal(int(resolution_x))})
ry = int({_py_literal(int(resolution_y))})
mask = {_py_literal(bool(mask_enabled))}
hud = {_py_literal(bool(show_hud))}
if not override:
    proj_dir = unreal.SystemLibrary.get_project_directory()
    ts = int(time.time())
    override = os.path.join(proj_dir, "Saved", "Screenshots", f"MCP_{{ts}}.png")
os.makedirs(os.path.dirname(override), exist_ok=True)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
try:
    unreal.AutomationLibrary.take_high_res_screenshot(rx, ry, override, mask_enabled=mask)
    ok = True
except Exception as e:
    ok = False
    err = str(e)
print("<<<__MCP_RESULT__>>>")
print(json.dumps({{"file_path": override, "dispatched": ok}}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": marker.get("dispatched", False),
        "file_path": marker.get("file_path", file_path),
        "raw": response,
    }


@mcp.tool()
def get_editor_selection() -> Dict[str, Any]:
    """Get the current Content Browser and Level Outliner selections.

    Returns:
        ``{success, selected_assets: [path,...], selected_actors: [{name, class},...]}``.
    """
    code = """
import unreal, json
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_selected_level_actors() if eas else []
try:
    assets = unreal.EditorUtilityLibrary.get_selected_asset_data()
except Exception:
    assets = []
out = {
    "selected_assets": [str(a.package_name) for a in assets],
    "selected_actors": [
        {"name": a.get_actor_label(), "class": a.get_class().get_name()}
        for a in actors
    ],
}
print("<<<__MCP_RESULT__>>>")
print(json.dumps(out))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    result = {"success": bool(response.get("success", False))}
    result.update(marker)
    return result


@mcp.tool()
def get_compile_diagnostics(blueprint_name: str) -> Dict[str, Any]:
    """Compile a Blueprint and return structured errors / warnings / notes.

    Calls the C++ ``compile_blueprint_with_diagnostics`` handler, which runs
    ``FKismetEditorUtilities::CompileBlueprint`` against a local
    ``FCompilerResultsLog`` and serializes each ``FTokenizedMessage``.

    Requires the UnrealMCP C++ module to be rebuilt (Build.bat) — the
    Python-only ``compile_blueprint`` tool still works without it.

    Args:
        blueprint_name: Blueprint name OR full asset path
            (e.g. ``BP_PRKEnemy`` or ``/Game/AI/BP_PRKEnemy``).

    Returns:
        ``{success, name, path, status, num_errors, num_warnings, errors: [...],
        warnings: [...], notes: [...]}``. Each message entry: ``{severity, text}``.
    """
    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}
    try:
        response = unreal.send_command("compile_blueprint_with_diagnostics", {
            "blueprint_name": blueprint_name,
        })
        if not response:
            return {"success": False, "message": "No response from Unreal"}
        if response.get("status") == "error":
            return {
                "success": False,
                "message": response.get("error", "Unknown error"),
                "note": "If the error is 'Unknown command', the C++ side needs a rebuild "
                        "(Build.bat PRKEditor Win64 Development) to register "
                        "compile_blueprint_with_diagnostics.",
            }
        result = response.get("result") if "result" in response else response
        if isinstance(result, dict):
            result["success"] = result.get("num_errors", 0) == 0
        return result
    except Exception as e:
        logger.error(f"get_compile_diagnostics error: {e}")
        return {"success": False, "message": str(e)}


@mcp.tool()
def get_asset_references(asset_path: str, direction: str = "referencers", max_results: int = 200) -> Dict[str, Any]:
    """Walk the AssetRegistry dependency graph for an asset.

    Args:
        asset_path: ``/Game/...`` package path.
        direction: ``referencers`` (who depends on this) or ``dependencies``
            (what this depends on).
        max_results: Cap on returned paths.

    Returns:
        ``{success, asset, direction, total, paths: [...]}``.
    """
    code = f"""
import unreal, json
asset_path = {_py_literal(asset_path)}
direction = {_py_literal(direction)}
limit = int({_py_literal(int(max_results))})
ar = unreal.AssetRegistryHelpers.get_asset_registry()
pkg_name = asset_path.split('.')[0]
if direction == "dependencies":
    deps = ar.get_dependencies(pkg_name)
else:
    deps = ar.get_referencers(pkg_name)
deps = [str(d) for d in (deps or [])]
total = len(deps)
out = {{"asset": asset_path, "direction": direction, "total": total, "paths": deps[:limit]}}
print("<<<__MCP_RESULT__>>>")
print(json.dumps(out))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    result = {"success": bool(response.get("success", False))}
    result.update(marker)
    return result


@mcp.tool()
def get_stat_data(stat_group: str = "unit") -> Dict[str, Any]:
    """Toggle a ``stat <group>`` display in the editor viewport.

    UE's stat system writes to the screen and to ``LogStats``, not to a
    Python-readable channel. This tool just toggles the overlay; pair it with
    ``tail_output_log(contains="LogStats")`` for the numbers, or use
    ``capture_viewport`` to grab the on-screen readout.

    Common groups: ``unit``, ``fps``, ``gpu``, ``game``, ``engine``,
    ``streaming``, ``levels``, ``slowtask``.

    Args:
        stat_group: Stat group name (no ``stat`` prefix).

    Returns:
        ``{success, group}``.
    """
    code = f"""
import unreal, json
group = {_py_literal(stat_group)}
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "stat " + group)
print("<<<__MCP_RESULT__>>>")
print(json.dumps({{"group": group}}))
print("<<</__MCP_RESULT__>>>")
"""
    response = _editor_run_python(code)
    marker = _extract_json_marker(response) or {}
    return {
        "success": bool(response.get("success", False)),
        "group": marker.get("group", stat_group),
        "raw": response,
    }


# ---------------------------------------------------------------------------
# Errant Worlds (Paths / Biomes / Landscape) editor automation
#
# Thin wrappers over Content/Python/prk_errant.py. The Errant plugins expose
# their authoring API as reflected BlueprintCallable libraries, so the work is
# done in-editor by prk_errant and these tools just marshal args/results.
#
# Design: every generate/update is a NON-BLOCKING "kick" — Errant generation
# advances on editor ticks/async tasks, so a blocking poll inside one call would
# stall the game thread it depends on (deadlock). Pair a *_generate / *_update
# with repeated *_status calls until done (e.g. biomes: status.in_progress False).
#
# Two standing caveats surfaced in the results:
#  * License gate: an unlicensed plugin refuses generation (biomes returns
#    start_result PERSISTENT_GENERATION_ERROR; landscape/paths log "invalid
#    license"). The key is entered via each plugin's own UI — not scriptable.
#  * Landscape edit layers: the landscape-adjustment stage silently no-ops under
#    unattended scripting and needs an attended "Force Generate". ok/success does
#    not prove the terrain changed.
# ---------------------------------------------------------------------------

def _run_errant(func: str, **kwargs) -> Dict[str, Any]:
    """Call prk_errant.<func>(**kwargs) inside the editor and return its dict.

    prk_errant functions return a JSON dict with an ``ok`` flag; this maps that
    to the MCP ``success`` convention while preserving every field."""
    kw = ", ".join(f"{k}={_py_literal(v)}" for k, v in kwargs.items())
    code = (
        "import json\n"
        "import prk_errant\n"
        f"res = prk_errant.{func}({kw})\n"
        'print("<<<__MCP_RESULT__>>>")\n'
        "print(json.dumps(res))\n"
        'print("<<</__MCP_RESULT__>>>")\n'
    )
    response = _editor_run_python(code)
    marker = _extract_json_marker(response)
    if marker is None:
        return {
            "success": False,
            "message": "No result from prk_errant — is the editor running and "
                       "Content/Python/prk_errant.py importable?",
            "raw": response,
        }
    out = {"success": bool(marker.get("ok", False))}
    out.update(marker)
    return out


@mcp.tool()
def errant_paths_regenerate(
    selected_only: bool = False,
    full_rebuild: bool = False,
    generate_landscape_biomes: bool = True,
    generate_physics: bool = True,
) -> Dict[str, Any]:
    """Regenerate Errant Paths via the BLOCKING driver.

    selected_only: only the selected path, else all dirty paths.
    full_rebuild: force the full EEpUpdateFlags::All mask (no prior spline-nudge
    needed). CAVEAT: this driver completes height/spawn/clear but its biomes/
    landscape-adjustment stage (material weightmap, e.g. dirt) aborts at a 10s
    watchdog — use ``errant_paths_incremental_rebuild`` to land the weightmap."""
    return _run_errant(
        "paths_regenerate",
        selected_only=selected_only,
        full_rebuild=full_rebuild,
        generate_landscape_biomes=generate_landscape_biomes,
        generate_physics=generate_physics,
    )


@mcp.tool()
def errant_paths_incremental_rebuild(path_name: Optional[str] = None) -> Dict[str, Any]:
    """Mark EpPath(s) dirty (All) and start the plugin's INCREMENTAL tick-driven update.
    NOT the Force Generate equivalent: outside the Errant Paths editor mode the plugin's
    ticker only advances paths whose update already started, so this does nothing there
    (verified 2026-10-08: no log line, state and output unchanged). Use
    ``errant_path_regenerate`` (blocking) instead. The weightmap claim once made here is
    void: the blocking route re-ran the carve AND kept Ground_Sand at 1.0 on the river
    centreline (2026-10-08, 3 paths x 3 points; sand was already 1.0 before, so it shows the
    blocking route does not lose the weightmap rather than an A/B from an unpainted state).

    path_name: actor name or label of one EpPath; omit for every loaded EpPath.
    Requires the StartErrantPathIncrementalRebuild bridge wrapper compiled into
    PRKEditor (the result says so if it is missing)."""
    return _run_errant("paths_incremental_rebuild", path_name=path_name)


@mcp.tool()
def errant_paths_status(selected_only: bool = False) -> Dict[str, Any]:
    """Report reflected Errant Paths state: path count, selected path, its
    connections, and per-path spawned-component count (a coarse 'has generated'
    proxy — exact dirtiness is not reflected from Python)."""
    return _run_errant("paths_status", selected_only=selected_only)


@mcp.tool()
def errant_biomes_generate(
    bounds: Optional[List[Any]] = None,
    component_xys: Optional[List[List[int]]] = None,
    dirty_only: bool = False,
) -> Dict[str, Any]:
    """Kick Errant Biomes generation (non-blocking).

    Provide exactly one of:
      * ``bounds`` — one world box [[minX,minY,minZ],[maxX,maxY,maxZ]] or a list of
        such boxes; generates every component intersecting them.
      * ``component_xys`` — explicit [[x,y],...] component indices.
      * neither — generates all currently-loaded components.
    ``dirty_only`` skips already-generated, unchanged components.

    Returns the authoritative ``start_result`` enum — this is the license check
    (PERSISTENT_GENERATION_ERROR ⇒ likely unlicensed). Poll
    ``errant_biomes_status`` until ``in_progress`` is False."""
    return _run_errant(
        "biomes_generate",
        bounds=bounds,
        component_xys=component_xys,
        dirty_only=dirty_only,
    )


@mcp.tool()
def errant_biomes_status(sample_world_xy: Optional[List[float]] = None) -> Dict[str, Any]:
    """Poll Errant Biomes generation. ``in_progress`` is the authoritative
    completion signal; also reports loaded and dirty component XY counts.
    ``sample_world_xy`` adds an errant_biomes_sample at that point."""
    return _run_errant("biomes_status", sample_world_xy=sample_world_xy)


@mcp.tool()
def errant_biomes_sample(
    world_xy: List[float],
    mask_paths: Optional[List[str]] = None,
    prefer_pie: bool = False,
) -> Dict[str, Any]:
    """Errant Biomes probe at [x, y]: the painted biome (index + asset name) and the
    value of each mask asset there ("Sampled" + value, or "NothingFound").

    mask_paths defaults to the road foliage masks the PRK road descriptions write
    (EM_RemoveFoliage, EM_RemoveFoliage_Wide). Pass other hand-painted or Errant Paths
    mask asset paths to probe them. Read-only. Bridge-backed
    (UPRKEditorAIBridge.SampleErrantBiomesAtLocation)."""
    return _run_errant("biomes_sample", world_xy=world_xy, mask_paths=mask_paths,
                       prefer_pie=prefer_pie)


@mcp.tool()
def errant_biomes_foliage_query(
    world_xy: List[float],
    radius_m: float = 5.0,
    species: Optional[str] = None,
) -> Dict[str, Any]:
    """Runtime Errant Biomes foliage within radius_m of [x, y] (editor world): total and
    density per m2, counts per species and per mesh, and per instance component its
    container and runtime spawner component (generation ComponentXY,
    LocalInstancesLocationOffset, AssetNumPartitions, WorldBounds3D). species filters by
    a substring of the species or mesh name. Instances exist only in partitions the
    runtime spawner has populated (near the editor view). Read-only."""
    return _run_errant("biomes_foliage_query", world_xy=world_xy, radius_m=radius_m,
                       species=species)


@mcp.tool()
def errant_paths_cross_section(
    path: str,
    fraction: float = 0.5,
    half_width_m: float = 12.0,
    step_m: float = 1.0,
    along_m: Optional[List[float]] = None,
    foliage_radius_m: float = 0.75,
    mask_paths: Optional[List[str]] = None,
) -> Dict[str, Any]:
    """Cross-section of an Errant Paths road (EpPath actor label or name) at ``fraction``
    of its PostSpawnSpline centreline. Per perpendicular offset (-half_width_m..+half_width_m,
    step_m; positive = left of the spline direction): final landscape layer weights and
    height, road mask values (default EM_RemoveFoliage/_Wide) and runtime foliage density
    per m2 (total and per species), averaged over along-road positions ``along_m``
    (default [-4,-2,0,2,4]) in discs of foliage_radius_m. Read-only. Bridge-backed."""
    return _run_errant("paths_cross_section", path=path, fraction=fraction,
                       half_width_m=half_width_m, step_m=step_m,
                       along_m=along_m if along_m is not None else [-4, -2, 0, 2, 4],
                       foliage_radius_m=foliage_radius_m, mask_paths=mask_paths)


@mcp.tool()
def errant_biomes_exclusion_check(
    world_xy: List[float],
    source: str,
    threshold: float = 0.9,
    half_m: float = 15.0,
    search_m: float = 15.0,
    cell_m: float = 1.0,
    radius_m: float = 40.0,
    species: Optional[str] = None,
) -> Dict[str, Any]:
    """Does runtime foliage respect an exclusion source around [x, y]?
    source = "mask:<mask asset path>" or "layer:<landscape LayerName>". Cells (cell_m) of the
    +/-half_m square above threshold form the exclusion; their mean foliage density is
    compared with the area average over radius_m, at zero shift and at every cell shift
    within +/-search_m. Returns density_under_m2, area_avg_m2, ratio, best_shift_m,
    best_density_m2 and verdict: "ok" (excluded in place), "displaced" (excluded at an
    offset: a coordinate/registration bug), "not_excluded". Read-only. Bridge-backed."""
    return _run_errant("biomes_exclusion_check", world_xy=world_xy, source=source,
                       threshold=threshold, half_m=half_m, search_m=search_m,
                       cell_m=cell_m, radius_m=radius_m, species=species)


@mcp.tool()
def errant_biomes_spawner_info(
    world_xy: List[float],
    prefer_pie: bool = False,
) -> Dict[str, Any]:
    """Errant Biomes generation/spawner probe at [x, y]: the generation component XY and
    UV there, whether that component is dirty, and every runtime spawner component whose
    WorldBounds3D contains the point (container, ComponentXY, LocalInstancesLocationOffset,
    species, AssetNumPartitions, WorldBounds3D). Read-only. Bridge-backed
    (UPRKEditorAIBridge.SampleErrantBiomesSpawnerInfo)."""
    return _run_errant("biomes_spawner_info", world_xy=world_xy, prefer_pie=prefer_pie)


@mcp.tool()
def errant_paths_describe(path: str) -> Dict[str, Any]:
    """Everything driving one Errant Paths road (EpPath actor label or name): description
    class, every AdjustLists entry (Weightmaps, BiomeMaskPath, bAffectsHeight, strategy
    class + LateralExpand_WS / LateralFalloff_WS / LongitudinalFalloff_WS / curve paths),
    SplineConnections, MainPathSpline + PostSpawnSpline world points, the external package
    file time and unsaved state, and the adjustment update state (bridge
    GetErrantPathAdjustmentState). Read-only; soft refs read as text, never loaded."""
    return _run_errant("paths_describe", path=path)


def _parse_biomes_generation_history(log_dir: str, component_xy: Optional[List[int]], max_entries: int) -> Dict[str, Any]:
    """Errant Biomes generation runs from PRK*.log files: the LogBiomes 'Setting up persistent
    generation state' / 'Setting up Persistent state for generation.' block, its 'Components:'
    and 'Biomes:' lines, 'Starting generation of N components' and 'Generation Done'."""
    import datetime
    import glob
    import re

    stamp = re.compile(r"^\[(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]\[\s*\d+\](\w+): (.*)$")

    def when(m):
        y, mo, d, h, mi, s, ms = (int(m.group(i)) for i in range(1, 8))
        return datetime.datetime(y, mo, d, h, mi, s, ms * 1000, tzinfo=datetime.timezone.utc)

    def iso(t):
        return None if t is None else {"utc": t.isoformat(timespec="milliseconds"),
                                        "local": t.astimezone().isoformat(timespec="milliseconds")}

    files = sorted(glob.glob(os.path.join(log_dir, "PRK*.log")), key=os.path.getmtime)
    runs = []
    for path in files:
        current = None
        expect_detail = False
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if expect_detail and not line.startswith("["):
                    text = line.strip()
                    if current is not None and text.startswith("Components:"):
                        current["components"] = [[int(a), int(b)] for a, b in re.findall(r"X=(-?\d+) Y=(-?\d+)", text)]
                        continue
                    if current is not None and text.startswith("Biomes:"):
                        current["biomes"] = [b.strip() for b in text[len("Biomes:"):].split(",") if b.strip()]
                        continue
                if "LogBiomes" not in line:
                    expect_detail = False
                    continue
                m = stamp.match(line.rstrip("\r\n"))
                if not m or m.group(8) != "LogBiomes":
                    continue
                msg = m.group(9)
                t = when(m)
                if msg.startswith("Setting up persistent generation state") or msg.startswith("Setting up Persistent state for generation"):
                    if current is None or current.get("generation_start") is not None:
                        if current is not None:
                            runs.append(current)
                        current = {"log_file": os.path.basename(path), "start": t, "components": [], "biomes": [],
                                   "generation_start": None, "end": None, "component_count": None}
                    expect_detail = msg.startswith("Setting up Persistent state for generation")
                elif msg.startswith("Starting generation of") and current is not None:
                    n = re.search(r"of (\d+) components", msg)
                    current["component_count"] = int(n.group(1)) if n else None
                    current["generation_start"] = t
                    expect_detail = False
                elif msg.startswith("Generation Done") and current is not None:
                    current["end"] = t
                    runs.append(current)
                    current = None
                    expect_detail = False
        if current is not None:
            runs.append(current)

    if component_xy is not None:
        key = [int(component_xy[0]), int(component_xy[1])]
        runs = [r for r in runs if key in r["components"]]
    total = len(runs)
    out_runs = []
    for r in sorted(runs, key=lambda r: r["start"], reverse=True)[:max(1, int(max_entries))]:
        out_runs.append({
            "log_file": r["log_file"],
            "start": iso(r["start"]),
            "generation_start": iso(r["generation_start"]),
            "end": iso(r["end"]),
            "duration_s": round((r["end"] - r["start"]).total_seconds(), 3) if r["end"] else None,
            "generation_s": round((r["end"] - r["generation_start"]).total_seconds(), 3)
            if r["end"] and r["generation_start"] else None,
            "status": "done" if r["end"] else "no 'Generation Done' line (still running, crashed, or log rotated)",
            "component_count": r["component_count"],
            "components": r["components"],
            "biomes": r["biomes"],
        })
    return {"ok": True, "log_dir": log_dir, "files_scanned": [os.path.basename(p) for p in files],
            "component_filter": component_xy, "matching_runs": total, "runs": out_runs,
            "note": "log stamps are UTC (UE default); 'local' is this machine's timezone"}


@mcp.tool()
def errant_biomes_generation_history(
    component_xy: Optional[List[int]] = None,
    max_entries: int = 20,
    log_dir: Optional[str] = None,
) -> Dict[str, Any]:
    """Errant Biomes generation runs parsed from Saved/Logs/PRK*.log (PRK.log, PRK-backup-*.log,
    other PRK_N.log instances): per run start / generation start / end (UTC log stamp and
    local), duration, component XYs, biomes and log file, newest first. component_xy filters
    to runs that generated that component. Parsed in the MCP server process: no editor
    round-trip, no load on the editor game thread."""
    log_dir = log_dir or os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                       "..", "..", "..", "Saved", "Logs"))
    try:
        res = _parse_biomes_generation_history(log_dir, component_xy, max_entries)
    except Exception as e:  # noqa: BLE001
        return {"success": False, "message": f"{type(e).__name__}: {e}"}
    out = {"success": True}
    out.update(res)
    return out


@mcp.tool()
def errant_mask_freshness(world_xy: List[float], radius_m: float = 10.0) -> Dict[str, Any]:
    """Errant Biomes mask tiles (ErrantPathsMask / MaskTextureData MaskData_<cx>_<cy>_*) of the
    generation component under [x, y], each mapped to its mask asset via the asset registry
    GUID tag, with file times; the EpPaths whose spline passes within radius_m, with their
    package file times, unsaved state and the masks they write; and a flag on every tile
    older than an edit of a nearby path that writes it. Filesystem + asset registry only."""
    return _run_errant("mask_freshness", world_xy=world_xy, radius_m=radius_m)


@mcp.tool()
def errant_landscape_brushes_at(world_xy: List[float]) -> Dict[str, Any]:
    """Every loaded Errant Landscape brush actor (stamps like Plain_7_C, flat, noise, blur)
    whose bounds cover [x, y]: class, label, per brush component priority, decoded update
    mask, enabled/hidden state, target bridge, blend method / layer mode / source / stamp
    settings, painted weightmap names, and bounds; sorted by priority. Read-only."""
    return _run_errant("landscape_brushes_at", world_xy=world_xy)


@mcp.tool()
def probe_heatmap(
    center_xy: List[float],
    size_m: float = 40.0,
    cell_m: float = 1.0,
    channels: Optional[List[str]] = None,
    out_dir: Optional[str] = None,
) -> Dict[str, Any]:
    """PNG heatmaps around [x, y] (size_m square, cell_m cells, max 128x128). channels:
    "mask:<asset path>", "layer:<LayerName>", "foliage" or "foliage:<species>", "height".
    One PNG per channel plus a side-by-side composite in out_dir (default
    <temp>/probe_heatmaps, never the project tree); returns paths and min/max/mean per
    channel. Columns +X, rows +Y, same as capture_top_down. Read-only."""
    return _run_errant("probe_heatmap", center_xy=center_xy, size_m=size_m, cell_m=cell_m,
                       channels=channels, out_dir=out_dir)


@mcp.tool()
def errant_landscape_update(start_edit_mode: bool = True) -> Dict[str, Any]:
    """DISABLED — always refuses, with no editor round-trip.

    On 2026-09-01 this tool (StartEditMode2 + ForceUpdateAllLandscape2, unattended,
    with no matching EndEditMode2) wiped every landscape component's merged weightmap
    allocations across the whole map, and that state was saved. Errant Landscape brush
    updates are a user-driven action inside the Errant editor mode only.

    Use ``errant_landscape_bridge_status`` (read-only) and
    ``errant_landscape_register_bridges`` (registration only, renders nothing)."""
    return {
        "success": False,
        "refused": True,
        "message": "errant_landscape_update is disabled: on 2026-09-01 StartEditMode2 + "
                   "ForceUpdateAllLandscape2 driven unattended wiped every landscape "
                   "component's weightmap allocations and the state was saved. Errant brush "
                   "updates are user-driven in the Errant editor mode only.",
        "use_instead": [
            "errant_landscape_bridge_status",
            "errant_landscape_register_bridges",
        ],
    }


@mcp.tool()
def errant_landscape_bridge_status() -> Dict[str, Any]:
    """Read-only Tier-3 status of the Errant Landscape brush graph.

    Reports which ElBridge actors are registered with the running brush subsystem
    (world-wide and per landscape), each landscape's edit layers with GUID + locked /
    visible flags, each bridge's layer GUID and whether that layer resolves, the loaded
    brush actors with priority / update mask / per-brush data-validation result, and
    brush-actor counts loaded vs on disk (World Partition actor descs).

    ``world_has_registered_bridges`` False while bridges are listed IS the
    "Errant Landscape update skipped due to missing landscape" /
    "Create New Errant Layer" failure; fix it with
    ``errant_landscape_register_bridges``. Creates no subsystem entry, renders nothing."""
    return _run_errant("landscape_bridge_status")


@mcp.tool()
def errant_landscape_register_bridges() -> Dict[str, Any]:
    """The ONE permitted Errant Landscape write: register loaded ElBridge actors with the
    running brush subsystem.

    Needed when the plugin's own PostLoad missed the Landscape actor — it resolves it via
    LoadSynchronous() on a soft ref that is null for a World Partition actor that has not
    streamed in yet, so nothing registers and nothing is logged. Symptom:
    "Errant Landscape update skipped due to missing landscape" and a
    "Create New Errant Layer" panel for a layer that already exists.

    Calls RegisterLandscapeBridge directly, so unlike the plugin's own re-registration path
    it forces no whole-world landscape update, and it dirties nothing. Caveat: registering the
    world's DEFAULT bridge makes the plugin re-register brushes that had no explicit bridge,
    queuing a per-brush update bounded to those brushes' own bounds — ordinary plugin
    behaviour, not a forced whole-world render."""
    return _run_errant("landscape_register_bridges")


@mcp.tool()
def errant_landscape_status() -> Dict[str, Any]:
    """Report reflected Errant Landscape state: edit-mode active flag and the
    brush actors present in the level. Shallow reflection only — use
    ``errant_landscape_bridge_status`` for bridge registration, edit layers and
    per-brush validation."""
    return _run_errant("landscape_status")


@mcp.tool()
def errant_stamp_list(class_name: Optional[str] = None) -> Dict[str, Any]:
    """Read-only inventory of the Errant stamp brushes in the editor world.

    Per brush: label, class, actor XY, actor scale X/Y (the stamp footprint in
    METRES, cross-checked against actor bounds), the stamp ``lod_bias``, the
    heightmap texture name and X size, and metres-per-texel. Metres-per-texel far
    below the landscape grid size is the sampling aliasing that serrates a crest.

    ``class_name`` filters on the exact generated class name (e.g. "LongMountain_C").
    Reads only — dirties nothing, renders nothing, saves nothing."""
    return _run_prk_module("prk_errant", "landscape_stamp_list", class_name=class_name)


@mcp.tool()
def errant_stamp_set_lod_bias(
    lod_bias: int,
    actor_labels: Optional[List[str]] = None,
    class_name: Optional[str] = None,
    dry_run: bool = True,
    max_actors: int = 64,
) -> Dict[str, Any]:
    """Set the stamp LOD bias on Errant stamp brushes (the permitted per-brush write).

    Edits ONE reflected property — ``heightmap_params.stamp_settings2.lod_bias``, the
    same edit the Details panel makes — then requests the plugin's own BOUNDED
    per-brush ``RequestUpdateLandscape`` for each brush whose write verified. It never
    enters the Errant edit mode and never calls the whole-world force update
    (``errant_landscape_update`` stays disabled: unattended, that wiped every
    landscape component's weightmap allocations on 2026-09-01). It spawns and deletes
    nothing and saves nothing.

    Raising the bias picks a coarser mip (size / 2^bias), band-limiting an oversized
    stamp texture toward the landscape grid — the fix for serrated stamped crests.

    Select with EXACTLY ONE of ``actor_labels`` (exact actor labels) or ``class_name``.
    ``dry_run`` defaults True: run it first, then apply to ONE canary actor, verify,
    and only then to the rest. Refuses while PIE is running, on a bias outside 0..8,
    on a missing/ambiguous selector, or above ``max_actors``.

    Leaves the affected heightmap/weightmap packages DIRTY and UNSAVED — the user
    saves the level."""
    return _run_prk_module(
        "prk_errant",
        "landscape_stamp_set_lod_bias",
        lod_bias=lod_bias,
        actor_labels=actor_labels,
        class_name=class_name,
        dry_run=dry_run,
        max_actors=max_actors,
    )


@mcp.tool()
def errant_stamp_configure(
    actor: str,
    location_xy: Optional[List[float]] = None,
    location_z: Optional[float] = None,
    yaw: Optional[float] = None,
    scale: Optional[Any] = None,
    invert: Optional[bool] = None,
    multiplier: Optional[float] = None,
    falloff: Optional[bool] = None,
    falloff_start: Optional[Any] = None,
    falloff_end: Optional[Any] = None,
    falloff_noise: Optional[float] = None,
    blend_method: Optional[str] = None,
    offset_method: Optional[str] = None,
    z_offset: Optional[float] = None,
    lod_bias: Optional[int] = None,
    weightmap: Optional[Dict[str, Any]] = None,
    heightmap: Optional[Dict[str, Any]] = None,
    priority: Optional[int] = None,
    update: bool = True,
    dry_run: bool = True,
    max_dirty_proxies: int = 12,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    fit_falloff_to_bounds: bool = False,
    allow_rotation_clip: bool = False,
    allow_equal_priority: bool = False,
) -> Dict[str, Any]:
    """Configure ONE Errant stamp brush (writes; dry_run defaults True).

    Seam / order guards (2026-10-09): Errant clips a stamp to its UNROTATED bounds, so a yaw / scale /
    falloff change that leaves the falloff rectangle outside that box (``rotation_clip.fit`` < 1) is refused
    as a straight seam; ``fit_falloff_to_bounds=True`` applies ``rotation_clip.fix`` (falloff x k, scale
    X/Y / k: same ground footprint) or ``allow_rotation_clip=True`` overrides. A priority / blend / placement
    change that leaves the stamp at the SAME priority as an overlapping brush of the other kind (Max/Min cap
    vs add/subtract) is refused (``equal_priority``: Errant orders equal priorities internally and a write
    can reshuffle them); ``allow_equal_priority=True`` overrides.

    Fields: actor ``location_xy`` [x, y], Z (``location_z``, cm; with BrushZLocation, neutral =
    landscape Z -100), ``yaw`` (deg) and ``scale`` (scale_z number, or [x, y, z]) through the root
    component's relative_location / relative_rotation / relative_scale3d (the Details path; a move
    counts the old and the new footprint's proxies); heightmap blend ``invert``, ``multiplier``,
    ``falloff``, ``falloff_start`` / ``falloff_end`` (number or [x, y]), ``falloff_noise``,
    ``blend_method`` (AddAndSubtract, OnlyAdd, OnlySubtract, Replace, Multiply, Max, Min,
    ReplaceIfOverThreshold, ReplaceIfUnderThreshold), ``offset_method`` (Manual, BrushZLocation)
    and ``z_offset`` (cm; set it explicitly where a class default is non-zero, e.g. WideCanyon
    -15700); stamp ``lod_bias`` (0..8); brush ``priority`` (int >= 0, ElBlueprintLibrary SetBrushPriority,
    read back); optional ``heightmap`` {texture: /Game/... Texture2D, input_type:
    StandardFullRange or StandardOnlyAdd, levels: {input:[lo,hi], output:[lo,hi]},
    clamp: [lo,hi] or null}. Texture/input_type replace the source with whole-struct
    write-back, compile wait and read-back (requires active Errant mode for real writes).
    Levels remap source values; input must be finite with lo < hi. Clamp limits the final
    blended height, so enabling it counts as a dig even for Max. Null disables clamping;
    and ONE ``weightmap`` entry, added, updated, or removed by layer name:
    {layer, texture (needed to add), channel, lod_bias, multiplier, invert, levels
    ({input:[lo,hi], output:[lo,hi]}), clamp ([lo,hi] or null), blend_method, falloff,
    falloff_start, falloff_end, falloff_noise, offset_method, z_offset}. A new entry copies
    the heightmap sampling and starts from offset Manual 0. To remove an entry, pass
    exactly {layer: "Ground_Sand", remove: true}; the old weight footprint is included
    by the component's PreEditChange update request.

    Refuses: PIE running (writes), editor world other than ``expect_world``, a target that is
    not exactly one stamp brush by label or name (bridges, non-stamp brushes, multiple
    matches), any weightmap change unless every loaded ElBridge PerLandscapeSettings
    WeightmapMethod is WeightBlendNoDefault, an update PREDICTED to dirty more than
    ``max_dirty_proxies`` landscape proxies (``predicted_proxies``: the plugin re-renders the
    UNROTATED brush bounds, new united with old, grown by any non-partial (blur) brush it reaches;
    ``footprint_proxies`` = the rotated footprints, reported too), a weightmap texture that is not
    loaded yet while UPRKEditorAIBridge.FinishAssetCompilation is not built, and (when priority,
    scale, blend_method, invert or multiplier change) a stamp that digs at a priority not above
    every Max-blend brush over its centre and corners (the errant_stamp_spawn rule; reported as
    ``dig_priority``).

    Write: one ScopedEditorTransaction, whole-struct write-back, every field read back
    (``verified``); then RequestUpdateLandscape on THAT component only (``update``). A loaded
    weightmap texture is waited for (``asset_compile``). Outside EL edit mode the result carries
    ``render_probe`` (landscape heights at 9 points per footprint BEFORE the write): pass it to
    ``errant_stamp_render_check`` in a LATER call, because an update outside EL mode can render
    nothing until a LodBias toggle (HW-0a, SW-Q1, 2026-10-08). Returns before / planned_after /
    diff, edit_mode_active and the dirty packages read in this call (``dirty_after_call`` is
    pre-tick: it never shows this update; re-check with ``errant_dirty_landscape``). Never saves,
    never touches the EL edit mode or a bridge."""
    return _run_prk_module(
        "prk_lake_ops", "stamp_configure",
        actor=actor, location_xy=location_xy, location_z=location_z, yaw=yaw, scale=scale,
        invert=invert, multiplier=multiplier,
        falloff=falloff, falloff_start=falloff_start, falloff_end=falloff_end,
        falloff_noise=falloff_noise, blend_method=blend_method, offset_method=offset_method,
        z_offset=z_offset, lod_bias=lod_bias, weightmap=weightmap, heightmap=heightmap, priority=priority, update=update,
        dry_run=dry_run, max_dirty_proxies=max_dirty_proxies, expect_world=expect_world,
        fit_falloff_to_bounds=fit_falloff_to_bounds, allow_rotation_clip=allow_rotation_clip,
        allow_equal_priority=allow_equal_priority,
    )


@mcp.tool()
def errant_stamp_render_check(
    actor: str,
    probe: Dict[str, Any],
    dry_run: bool = True,
    tol_cm: float = 2.0,
    max_dirty_proxies: int = 12,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
) -> Dict[str, Any]:
    """Verify that an errant_stamp_configure update rendered, and re-render it if not (writes only
    when stale; dry_run defaults True). Call it in a LATER call than the configure (the update
    lands on an editor tick) with the configure's ``render_probe``; each call advances ONE phase
    and returns the ``probe`` to pass next time:
      check    re-probe the landscape-only heights; any point moved > tol_cm (or the configure
               changed no height field) -> done. Stale -> LodBias original+1 (-1 at 8) through the
               guarded errant_stamp_configure path -> toggled;
      toggled  write the original LodBias back -> restored;
      restored re-probe against the pre-write heights -> done: rendered or still_stale (a
               priority-only change may legitimately change nothing).
    Refuses another world, PIE (writes), a probe for another actor and a LodBias that is not what
    the phase expects. Inside EL edit mode heights are stale: verdict stale_heights, no write.
    Never saves."""
    return _run_prk_module("prk_lake_ops", "stamp_render_check", actor=actor, probe=probe, dry_run=dry_run,
                           tol_cm=tol_cm, max_dirty_proxies=max_dirty_proxies, expect_world=expect_world)


@mcp.tool()
def errant_backup_external_actors(world: str = "ErrantWorld") -> Dict[str, Any]:
    """Copy a world's World Partition external actors (not in Perforce) to
    Saved/Backup_ErrantWorldActors_<YYYYMMDD-HHMM>/ with robocopy (/E into a new folder; the
    source is only read; log in that folder). Runs in the MCP SERVER process, no editor call.
    Writes BACKUP_OK.json (time, file count, bytes) only when the copy matches the source;
    errant_stamp_spawn requires one younger than its backup_max_age_h. Refuses when free
    disk < 2x the source. ErrantWorld is ~4.5 GB: minutes, blocking."""
    try:
        return prk_backup.backup_external_actors(world)
    except Exception as e:
        return {"success": False, "message": f"{type(e).__name__}: {e}"}


@mcp.tool()
def errant_stamp_spawn(
    stamp_class: str,
    x: float,
    y: float,
    yaw: float,
    scale: List[float],
    location_z: float,
    priority: int,
    invert: Optional[bool] = None,
    multiplier: Optional[float] = None,
    falloff: Optional[bool] = None,
    falloff_start: Optional[Any] = None,
    falloff_end: Optional[Any] = None,
    falloff_noise: Optional[float] = None,
    blend_method: Optional[str] = None,
    offset_method: Optional[str] = None,
    z_offset: Optional[float] = None,
    lod_bias: Optional[int] = None,
    weightmap: Optional[Dict[str, Any]] = None,
    heightmap: Optional[Dict[str, Any]] = None,
    label: Optional[str] = None,
    dry_run: bool = True,
    max_dirty_proxies: int = 12,
    backup_max_age_h: float = 12.0,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    fit_falloff_to_bounds: bool = False,
    allow_rotation_clip: bool = False,
    allow_equal_priority: bool = False,
) -> Dict[str, Any]:
    """Spawn ONE Errant stamp brush from the stamp library (writes; dry_run defaults True). The
    only permitted scripted brush spawn (the 2026-09-01 ban stays for raw scripts).

    Also guards ``rotation_clip`` and ``equal_priority`` with the same overrides and
    ``fit_falloff_to_bounds`` as errant_stamp_configure (unknown class defaults count as falloff on,
    start 0.5, end 1.0, and are re-checked after the class loads).

    ``stamp_class``: /Game/ErrantWorlds/ErrantLandscapeStampLibrary/... blueprint (package, object
    or _C path). ``scale`` [sx, sy, sz] (sx/sy = footprint metres), ``location_z`` cm, ``priority``
    int; blend / LOD / heightmap / weightmap fields as errant_stamp_configure.

    Guards, all listed in ``guards`` (a dry run reports every one, a write refuses if any fails):
    editor world == expect_world; PIE stopped; Errant Landscape edit mode ACTIVE; every ElBrushActor
    on disk loaded (bridge status loaded >= on-disk count); a BACKUP_OK.json from
    errant_backup_external_actors younger than backup_max_age_h; the class resolves in the asset
    registry to a Blueprint under an allow-listed stamp folder whose native parent is an
    ElBrushActor; the first update is predicted to dirty <= max_dirty_proxies proxies
    (``predicted_proxies``: the final UNROTATED box united with the CLASS-DEFAULT scale box the
    brush registers with, grown by non-partial brushes; a class not loaded yet is re-checked after
    its load); ``asset_compile_wait``: the class is resident or UPRKEditorAIBridge.FinishAssetCompilation
    is built (a real spawn blocks until the class's heightmap/weightmap textures finish building,
    reported as ``asset_compile``); a stamp that digs (blend Min /
    OnlySubtract / Replace / Multiply / thresholds, an inverted or negative AddAndSubtract, or may:
    class defaults unknown until loaded; a passed blend_method overrides the class default) must have a priority above every Max-blend brush covering
    the footprint centre or corners (a dig at <= a Max brush is erased, Lake_3); weightmap needs
    every bridge on WeightBlendNoDefault; the label is unused.

    Write: one transaction; spawn at (x, y, location_z) yaw; folder "ErrantLandscape"; tag
    "PRK_ScriptedStamp"; fields via the errant_stamp_configure code path; set_brush_priority; every
    field read back. Returns actor name/label, footprint and predicted proxies, the dirty packages
    read in this call (pre-tick: the update's proxies land later; re-check with
    errant_dirty_landscape) and the edit-mode note. Never saves."""
    return _run_prk_module(
        "prk_lake_ops", "stamp_spawn",
        stamp_class=stamp_class, x=x, y=y, yaw=yaw, scale=scale, location_z=location_z, priority=priority,
        invert=invert, multiplier=multiplier, falloff=falloff, falloff_start=falloff_start,
        falloff_end=falloff_end, falloff_noise=falloff_noise, blend_method=blend_method,
        offset_method=offset_method, z_offset=z_offset, lod_bias=lod_bias, weightmap=weightmap, heightmap=heightmap,
        label=label, dry_run=dry_run, max_dirty_proxies=max_dirty_proxies,
        backup_max_age_h=backup_max_age_h, expect_world=expect_world,
        fit_falloff_to_bounds=fit_falloff_to_bounds, allow_rotation_clip=allow_rotation_clip,
        allow_equal_priority=allow_equal_priority,
    )


@mcp.tool()
def errant_stamp_delete(
    actor: str,
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
) -> Dict[str, Any]:
    """Delete ONE stamp brush that errant_stamp_spawn created (writes; dry_run defaults True).
    Refuses any actor without the "PRK_ScriptedStamp" tag, another editor world, PIE running,
    Errant Landscape edit mode off.
    One transaction (Ctrl+Z restores it). Never saves."""
    return _run_prk_module("prk_lake_ops", "stamp_delete", actor=actor, dry_run=dry_run,
                           expect_world=expect_world)


@mcp.tool()
def errant_stamp_seam_scan(
    actors: Optional[List[str]] = None,
    center_xy: Optional[List[float]] = None,
    size_m: Optional[float] = None,
    include_axis_aligned: bool = False,
    min_run_m: float = 30.0,
    max_stamps: int = 150,
    strip_cell_cm: float = 300.0,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
) -> Dict[str, Any]:
    """Read-only: find stamps clipped by their UNROTATED bounds (straight seams) and stamps whose result depends
    on Errant's equal-priority order. For ``actors`` (labels/names) or every stamp whose bounds touch the
    ``center_xy`` / ``size_m`` square: rotation-clip ``fit`` (< 1 = clipped) and the ``fix`` that
    errant_stamp_configure(fit_falloff_to_bounds=True) would apply, same-priority peers of the other kind, and
    per bounds edge a 12-sample strip across the edge scored for a single-cell step (``edges``: hits, max_step_cm,
    longest continuous run in metres with its start/end XY, up to 40 hit XY). ``suspects`` = an edge run >=
    min_run_m: a clipped stamp leaves one long straight run, natural cliffs crossing an edge leave short scattered
    runs. Look at worst_run_xy before fixing. Axis-aligned stamps are skipped unless include_axis_aligned. Run
    with Errant edit mode OFF (heights are stale inside it). Writes nothing."""
    return _run_prk_module("prk_lake_ops", "stamp_seam_scan", actors=actors, center_xy=center_xy, size_m=size_m,
                           include_axis_aligned=include_axis_aligned, min_run_m=min_run_m, max_stamps=max_stamps,
                           strip_cell_cm=strip_cell_cm, expect_world=expect_world)


@mcp.tool()
def errant_edit_mode_set(
    active: bool,
    dry_run: bool = True,
    backup_max_age_h: float = 12.0,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    preserve_camera: bool = True,
) -> Dict[str, Any]:
    """Enter (active=True) or leave the Errant Landscape edit mode exactly as the toolbar does
    (bridge SetErrantLandscapeEditMode -> GLevelEditorModeTools ActivateMode/DeactivateMode
    "EM_ErrantLandscape" -> UElEdMode::Enter/Exit). Writes; dry_run defaults True.
    ``preserve_camera`` (default True): a real toggle that moves the level viewport camera is set back;
    ``camera`` reports before / after_toggle / restored.

    Leaving runs the plugin's FinishEdit: one synchronous update over the pending request bounds
    (expanded by overlapping non-partial brushes), then the collision commit, so traces read the new
    terrain afterwards. Guards: right world, PIE stopped, bridge function built, every brush on disk
    loaded, bridges registered; leaving also needs a backup younger than backup_max_age_h and an idle
    brush subsystem. Already in the requested state = ok, no call. Never saves. Never
    StartEditMode2/EndEditMode2 (subsystem flag only)."""
    return _run_prk_module("prk_lake_ops", "edit_mode_set", active=active, dry_run=dry_run,
                           backup_max_age_h=backup_max_age_h, expect_world=expect_world,
                           preserve_camera=preserve_camera)


@mcp.tool()
def errant_dirty_landscape() -> Dict[str, Any]:
    """Read-only: dirty LandscapeStreamingProxy packages, dirty Errant actor packages
    (ElBrushActor / ElBridge / EpPath) and dirty Errant Biomes / Paths content packages,
    plus edit_mode_active. Re-check after errant_stamp_configure and stop above its
    max_dirty_proxies."""
    return _run_prk_module("prk_lake_ops", "dirty_landscape")


@mcp.tool()
def errant_lake_survey(
    actor: str,
    water_z: float,
    grid_m: float = 10.0,
    perimeter_step_cm: float = 50.0,
    profiles: int = 4,
    sand_layer: str = "Ground_Sand",
    snow_layer: str = "Ground_Snow",
) -> Dict[str, Any]:
    """READ-ONLY survey of a stamped lake over the stamp's oriented footprint, at ``water_z``.

    Returns: deepest bed (z, xy, depth), water area (cells below water_z) and the number of
    connected water bodies (4-connected flood fill); every footprint-perimeter point below
    water_z (spill points, with contiguous runs); radial profiles (4 = +u, +v, -u, -v) with
    shoreline distance, max slope in the 40 m beyond the shore, and the final ``sand_layer``
    weight at the waterline; and ``snow_layer`` on the Errant edit layer over a 6x6 grid
    (bridge SampleLandscapeEditLayerWeights). Heights come from WorldStatic traces that keep
    landscape hits only; at most ~5000 traces per call (grid and perimeter step coarsen to
    fit, reported in ``plan``). ``stale_heights`` is true while the EL edit mode is active
    (collision is rebuilt only on mode exit)."""
    return _run_prk_module(
        "prk_lake_ops", "lake_survey",
        actor=actor, water_z=water_z, grid_m=grid_m, perimeter_step_cm=perimeter_step_cm,
        profiles=profiles, sand_layer=sand_layer, snow_layer=snow_layer,
    )


@mcp.tool()
def lake_water_place(
    stamp_actor: str,
    water_z: float,
    label: Optional[str] = None,
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
) -> Dict[str, Any]:
    """Create or update a lake water plane for a stamp (writes; dry_run defaults True).

    The actor (label default "<stampLabel>_Water") is a StaticMeshActor with
    /Game/PRK/World/Water/SM_LakeSurface + MI_LakeWater, NoCollision, no navigation
    relevance, no shadow, folder "Water", at the stamp's XY and yaw, Z = ``water_z``,
    scale = stamp footprint metres / 100 (the mesh is a 100 m plane). Refuses while PIE
    runs, on another editor world, on a non-stamp source, on duplicate labels, and on an
    existing actor of that label with a different mesh. One transaction; transform and
    component settings read back. Never saves."""
    return _run_prk_module(
        "prk_lake_ops", "lake_water_place",
        stamp_actor=stamp_actor, water_z=water_z, label=label, dry_run=dry_run, expect_world=expect_world,
    )


@mcp.tool()
def errant_path_set_points(
    path: str,
    points: Optional[List[Dict[str, Any]]] = None,
    level_tangents: Optional[Any] = None,
    rebuild: bool = False,
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
) -> Dict[str, Any]:
    """Move existing points of an EpPath's MainPathSpline_0 (writes; dry_run defaults True).

    ``points``: [{index, x, y, z}] in world cm. Refuses a non-EpPath, an index out of range,
    duplicate indices, PIE running and another editor world; never adds or deletes points.
    One transaction, read back within 1 cm, then (``rebuild``, default False) the blocking
    regeneration of THIS path (``errant_path_regenerate``, result under ``rebuild``): it
    FREEZES the editor until the path is done.

    ``level_tangents``: True (every point) or a list of indices: sets the world Z of those points'
    arrive AND leave tangents to 0, keeping X/Y (points may be passed with no position change, or
    omitted). Returns before / after points and tangents and an 11-sample Z profile (evenly spaced
    input keys, with distance); a dry run adds a Hermite-predicted after profile. Never saves."""
    return _run_prk_module(
        "prk_lake_ops", "path_set_points",
        path=path, points=points, level_tangents=level_tangents, rebuild=rebuild, dry_run=dry_run,
        expect_world=expect_world,
    )


@mcp.tool()
def errant_path_regenerate(
    path: str,
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    skip_biomes: bool = True,
    skip_physics: bool = False,
    retry: bool = True,
    height_tol_cm: float = 150.0,
    paint_min_coverage: float = 0.9,
) -> Dict[str, Any]:
    """Regenerate ONE EpPath (label or actor name) — the Force Generate equivalent that works
    outside the Errant Paths editor mode. BLOCKS THE EDITOR (progress dialog, game thread
    frozen) until the path is done; dry_run defaults True and makes no call.

    Verdict by description (``verdict_kind``): roads (EPD_DirtRoad / StoneRoad / MountainRoad)
    by their own paint layers at up to 120 centreline samples (coverage >= paint_min_coverage) and,
    for the carving StoneRoad / MountainRoad, landscape-only ground within height_tol_cm of the
    spline at every sample (skipped while the EL edit mode makes heights stale); rivers, streams
    and other descriptions by the level-surface check below. ``retry`` (default) regenerates a road
    once more when the first run is not verified (R8, 2026-10-08: paint 1% on the first regenerate
    after delete + create); water and other paths retry only when output is MISSING (no
    PostSpawnSpline, no surface meshes, paint short), never for a deviation or spread (a second run
    cannot move geometry). ``retry_reason`` says why; ``attempts`` lists every run.
    Own paint layers are the union of the path's adjust lists (re-read after every run: a recreated
    path has none until it generates), its templates' EPLA WeightmapAdjustments and a static table;
    an empty list skips the paint check, never fails or retries it. Water also gets a surface check
    from the loaded sub-actors' spline meshes: surface above the landscape bed at every probed
    segment, edges at +-half width not floating above the ground; water under the bed is a
    ``template_fault`` (spawn offset; errant_path_height_shape_check) and is never retried.

    Calls the plugin's public ``EpPathsLibrary.update_path_based_on_mask`` with the All
    update mask (checked against the plugin's compiled value). ``skip_biomes`` skips Errant
    Biomes foliage generation (the path's own biome-mask adjustment still runs). Refuses
    another editor world, PIE/Simulate running, zero or several matching EpPaths, and a path
    with an update already in progress. Before and after: PostSpawnSpline and main spline Z at
    11 length fractions, spawned mesh top Z per mesh (from SpawnedSubActors), final landscape
    layer weights at 3 centreline points, landscape bed Z at 5, dirtiness and the adjustment
    state. ``verified``: PostSpawnSpline within 5 cm of the main spline everywhere, and on a
    level path a surface-mesh top spread of at most 5 cm. Never saves."""
    return _run_prk_module(
        "prk_lake_ops", "path_regenerate",
        path=path, dry_run=dry_run, expect_world=expect_world, skip_biomes=skip_biomes,
        skip_physics=skip_physics, retry=retry, height_tol_cm=height_tol_cm,
        paint_min_coverage=paint_min_coverage,
    )


@mcp.tool()
def errant_path_create(
    desc_class: str,
    points: List[List[float]],
    label: str,
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    tangent_mode: str = "auto",
    connect_start: Optional[Dict[str, Any]] = None,
    connect_end: Optional[Dict[str, Any]] = None,
    connect_max_turn_deg: float = 30.0,
    regenerate: bool = False,
    allow_auto_regen: bool = True,
    drape: Optional[bool] = None,
    allow_cusps: bool = False,
    projection_off: Optional[bool] = None,
    allow_stone_outside_settlement: bool = False,
) -> Dict[str, Any]:
    """Create ONE Errant Paths road or stream from a waypoint list (writes; dry_run defaults True).

    EPD_StoneRoad is for towns and cities only (wilderness roads are DirtRoad or MountainRoad):
    guard ``stone_in_settlement`` fails unless every point lies inside a settlement footprint
    (each loaded PRKSettlementMarker's definition BoundsHalfExtent box at the marker transform;
    approximate, no wall circuits) or ``allow_stone_outside_settlement``.

    ``desc_class``: EPD_DirtRoad, EPD_StoneRoad, EPD_MountainRoad, EPD_River or EPD_Stream (allow-list;
    the Blueprint's native parent must be EpPathDescription).

    Always reported: ``carves`` (DirtRoad only paints; StoneRoad, MountainRoad, River, Stream carve;
    cross-checked after the spawn against the adjust lists' bAffectsHeight, a mismatch is a warning)
    and ``terrain_grade`` (max / p95 over 20 m windows along the points). A non-carving description
    with drape=False and a spline more than 50 cm off the ground gets a warning: that Z only floats
    the spawned splines. Carving roads (StoneRoad, MountainRoad) are refused when the points hold a
    cusp (a net turn > 150 deg within pi x 8 m of arc, e.g. an out-and-back spike; ``cusps`` lists
    them) unless ``allow_cusps``. ``projection_off`` (default: when draped) turns the path's own
    bProjectOntoGeometry off without re-draping. ``points``: [[x, y, z], ...] world cm, >= 2, consecutive
    points >= 1 m apart, every point over a loaded landscape proxy; the main spline gets exactly
    these points. ``tangent_mode``: "auto" or "level" (tangent Z = 0 everywhere; streams).
    ``label``: must be unused by any EpPath; the actor is also tagged PRK_ScriptedPath.

    Joints (Errant Paths connects path ENDS only; there is no mid-spline connection):
      {"path": L, "end": "start"|"end"}  our end placed on that end of path L. ``connect`` (default
          true) adds the EP connection via ConnectPaths, which turns our end tangent parallel to
          theirs, so it is refused when the road turns > ``connect_max_turn_deg`` through the joint;
          "connect": false places the end without a connection.
      {"path": L, "end": "onto"}  T-junction: our end on the closest point of L's main spline (its
          Z); no connection, L untouched.
      {"nearest_end_within_cm": N}  as "end", on the nearest loaded path end within N cm.
      Any form: ``snap_max_cm`` (default 3000), the most the given end point may move.

    Refuses PIE running and another editor world. One transaction, read back within 1 cm (points,
    label, tag, description class, connections). ``regenerate`` then runs errant_path_regenerate on
    it (BLOCKS the editor), whose verdict checks the description's own output (paint coverage; ground
    on the spline for carving roads; the level surface for water) and retries once.
    ``allow_auto_regen=False`` turns the path's
    bAllowAutomaticRegeneration off (read back): the Errant Paths mode never auto-generates it, while
    errant_path_regenerate still does.

    ``drape`` (default True for EPD_DirtRoad / EPD_StoneRoad): densify to <= 30 m, Z from landscape-only
    traces, and the path's own Desc projection off (read back). The road descriptions project onto any
    blocking collision, Errant Biomes trees included (R2a rode tree tops by up to 30 m, 2026-10-08).
    Never saves."""
    return _run_prk_module(
        "prk_lake_ops", "path_create",
        desc_class=desc_class, points=points, label=label, dry_run=dry_run, expect_world=expect_world,
        tangent_mode=tangent_mode, connect_start=connect_start, connect_end=connect_end,
        connect_max_turn_deg=connect_max_turn_deg, regenerate=regenerate,
        allow_auto_regen=allow_auto_regen, drape=drape, allow_cusps=allow_cusps, projection_off=projection_off,
        allow_stone_outside_settlement=allow_stone_outside_settlement,
    )


@mcp.tool()
def errant_path_replace_points(
    path: str,
    points: List[List[float]],
    drape: bool = True,
    tangent_mode: str = "auto",
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    allow_cusps: bool = False,
) -> Dict[str, Any]:
    """Replace ALL main-spline points of ONE path errant_path_create made (writes; dry_run defaults True).
    Refuses any path without the PRK_ScriptedPath tag (no override), points over no loaded landscape,
    a move of a CONNECTED end, and on a carving road (StoneRoad, MountainRoad) points with a cusp
    unless ``allow_cusps``. ``drape`` (default True): densify to <= 30 m, Z from landscape-only
    traces, the path's own Desc projection kept off. A real run first writes the old points and tangents
    to Saved/ZoneTerrain/<date>/paths/<label>_before.json. One transaction, read back within 1 cm; no
    regeneration (run errant_path_regenerate). Never saves."""
    return _run_prk_module("prk_lake_ops", "path_replace_points", path=path, points=points, drape=drape,
                           tangent_mode=tangent_mode, dry_run=dry_run, expect_world=expect_world,
                           allow_cusps=allow_cusps)


@mcp.tool()
def errant_path_delete(
    path: str,
    dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
) -> Dict[str, Any]:
    """Delete ONE EpPath that errant_path_create made (writes; dry_run defaults True). Refuses any
    path without the PRK_ScriptedPath tag, another editor world and PIE running. Destroying it runs
    the plugin's AEpPath::Destroyed: spawned sub-actors destroyed, connected paths disconnected
    (their packages get dirty), the landscape / biome adjustment cleared. Dry run lists the
    connected paths and sub-actor count. One transaction; Ctrl+Z restores only the actor. Never
    saves."""
    return _run_prk_module("prk_lake_ops", "path_delete", path=path, dry_run=dry_run, expect_world=expect_world)


@mcp.tool()
def errant_path_change_desc(
    path: str,
    desc_class: str,
    dry_run: bool = True,
    regenerate: bool = True,
    p4_revert_adds: bool = True,
    allow_cusps: bool = False,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    allow_stone_outside_settlement: bool = False,
) -> Dict[str, Any]:
    """Change ONE PRK_ScriptedPath path to another allow-listed description (writes; dry_run
    defaults True). Into EPD_StoneRoad the errant_path_create settlement rule applies
    (``allow_stone_outside_settlement``). The dry run exempts this path's own label from
    label_unused (the delete frees it) in the reported guards too. The description is fixed at spawn, so this is errant_path_delete then
    errant_path_create with the same label, the exact points (drape off, Z kept), the same tangent
    mode, auto-regeneration flag, projection (kept off when it was off or the new type is a draped
    road type) and connections (each connected end reconnected to the same end of the same path;
    this path becomes the ConnectPaths secondary); carve and cusp checks apply; ``regenerate``
    (default) runs the verified errant_path_regenerate (one retry; BLOCKS the editor).

    Before the delete: a snapshot (points, tangents, connections, label, flags, projection,
    descriptions) in Saved/ZoneTerrain/<date>/paths/<label>_before_<newdesc>.json and the list of
    the path's and its loaded sub-actors' external-actor package files. ``p4_revert_adds``: after
    the recreate, `p4 fstat` on exactly those files and `p4 revert` of ONLY the ones opened for add
    (localhost:1666, Eric, PRK_Workspace; never other files, never a changelist edit); every file
    and action is reported. The dry run returns the whole plan (path_create's dry run, files, p4
    actions) and writes nothing. A failure after the delete names the snapshot for a manual
    restore. Never saves."""
    return _run_prk_module("prk_lake_ops", "path_change_desc", path=path, desc_class=desc_class, dry_run=dry_run,
                           regenerate=regenerate, p4_revert_adds=p4_revert_adds, allow_cusps=allow_cusps,
                           expect_world=expect_world, allow_stone_outside_settlement=allow_stone_outside_settlement)


@mcp.tool()
def errant_road_profile_fit(
    path: Optional[str] = None,
    points: Optional[List[List[float]]] = None,
    max_grade: float = 0.12,
    smoothing_m: float = 50.0,
    sample_m: float = 10.0,
    pin_start: bool = True,
    pin_end: bool = True,
    level_ranges: Optional[List[List[float]]] = None,
    monotonic: Optional[str] = None,
) -> Dict[str, Any]:
    """READ-ONLY: a grade-capped road Z profile on the real ground. Give ``path`` (its main spline
    is sampled) or ``points`` [[x, y, z], ...] (the straight segments are sampled). Every sample_m:
    a landscape-only ground trace (at most 5000); moving average over smoothing_m; optional
    ``monotonic`` "up" / "down"; ``level_ranges`` [[s0_m, s1_m(, z_cm)], ...] held level (a hairpin
    bench; z defaults to the smoothed mean); ends pinned to the given / spline end Z (junctions);
    every sample clamped into the envelope the pins allow, then forward / backward slope-limit passes
    at max_grade. Returns ``points`` for errant_path_create / errant_path_replace_points(drape=False),
    max / p95 grade over 20 m, cut / fill and ``infeasible_pins`` (pins steeper than max_grade).
    ``stale_heights`` while the EL edit mode is on."""
    return _run_prk_module("prk_lake_ops", "road_profile_fit", path=path, points=points, max_grade=max_grade,
                           smoothing_m=smoothing_m, sample_m=sample_m, pin_start=pin_start, pin_end=pin_end,
                           level_ranges=level_ranges, monotonic=monotonic)


@mcp.tool()
def errant_ghyll_floor_trace(
    guide_points: List[List[float]],
    station_m: float = 20.0,
    half_width_m: float = 20.0,
    cross_step_m: float = 1.0,
    max_drift_m: float = 12.5,
    max_traces: int = 6000,
) -> Dict[str, Any]:
    """READ-ONLY: follow a ghyll / valley floor along ``guide_points`` ([[x, y(, z)], ...] cm). At
    stations every station_m, a landscape-only cross-section of +-half_width_m every cross_step_m;
    the lowest point within max_drift_m of the previous station's offset (continuous thalweg
    tracking); Z = min(ground, previous Z), so the bed never rises downstream and never sits above
    the ground. Refused when it could need more than max_traces traces. Returns ``points`` for
    errant_path_create (EPD_Stream, tangent_mode "level") and bed stats (offsets, clamped rises,
    head / end Z). Heights are stale while the EL edit mode is on."""
    return _run_prk_module("prk_lake_ops", "ghyll_floor_trace", guide_points=guide_points, station_m=station_m,
                           half_width_m=half_width_m, cross_step_m=cross_step_m, max_drift_m=max_drift_m,
                           max_traces=max_traces)


@mcp.tool()
def errant_road_qa(path: str, sample_m: float = 10.0, window_m: float = 20.0) -> Dict[str, Any]:
    """READ-ONLY road report: spline grade and ground grade max / p95 over window_m windows,
    ground - spline p50 / p95 / max (signed, cm), coverage of the path's own paint layers >= 0.5 on
    the centreline (bridge SampleLandscapeLayerWeights), cusps of the main-spline points, and
    ``carves`` (table) vs ``carves_described`` (adjust lists), plus ``ground_minus_spline_abs_cm``
    (p50 / p95 / max of |ground - spline|, as the status tables report). At most 600 samples (sample_m
    coarsens). Heights are stale while the EL edit mode is on. Samples are ON the centreline, XY
    rounded to 0.1 cm: EPD_DirtRoad's Path_Dirt (expand 100 + falloff 280 cm) covers one vertex row
    on axis-aligned stretches (1.0 centre, 0.5 at +-2 m, 0 at +-4 m), so an offset sample misses it."""
    return _run_prk_module("prk_lake_ops", "road_qa", path=path, sample_m=sample_m, window_m=window_m)


@mcp.tool()
def errant_path_height_shape_check(desc_classes: Optional[List[str]] = None, tol_cm: float = 10.0) -> Dict[str, Any]:
    """READ-ONLY: every height strategy of each allow-listed path description (default all),
    resolved EPD -> TemplateGroup template (EPT) -> its LandscapeAdjustmentSpline component's
    LandscapeBiomesAdjustmentPropertiesClass (EPLA) -> HeightAdjustments[i].AdjustmentStrategy
    (Offset Z, LateralExpand_WS, LateralHeightShapeCurve), with the EPD's own
    LandscapeBiomesAdjustmentOverrides height modifiers applied. Height-shape curves are WORLD cm
    added to the offset across x in [-1, 1] (sampled at 41 points). Flags: ``levee`` (a cutting
    strategy whose offset + curve rises above +tol_cm: EPD_Stream on HeightShapeCurve_River was
    -140 + 450) and ``shared_curve`` (one curve serving offsets that differ more than 2x).
    Spawn offsets: each template's EpPathTemplateSplineMeshComponent (inherited overrides included)
    PathTemplateCompSettings.RandomOffsetData (RandomLocation Z, RandomScale) against its carve bed
    (deepest enabled strategy Offset Z + curve(0)): ``water_below_bed`` (EPT_Stream was -325 over a
    -135 bed) and ``copied_offset`` (identical offsets over different beds). The spawner reads
    RandomOffsetData, never the component's relative location or spline-mesh start/end scale
    (anchors are recomputed on every Blueprint compile). Loads the description / template / EPLA
    Blueprints and the curves after a registry check; never writes."""
    return _run_prk_module("prk_lake_ops", "path_height_shape_check", desc_classes=desc_classes, tol_cm=tol_cm)


@mcp.tool()
def errant_foliage_band_density(
    polyline: List[List[float]],
    bands_m: Optional[List[List[float]]] = None,
    categories: Optional[Dict[str, List[str]]] = None,
    max_instances: int = 300000,
) -> Dict[str, Any]:
    """READ-ONLY: foliage instances per hectare in signed-distance bands across a polyline
    ([[x, y], ...] cm; ``bands_m`` [[d0, d1], ...] metres, positive = LEFT of the point order,
    negative = right; the ends are excluded), per category. Reads the runtime ISM instances of the
    ALREADY-LOADED Errant Biomes actors (never loads; runtime foliage exists only near the editor
    view) in the polyline box, nearest segment by a grid hash. Category: first ``categories`` entry
    (default box_elder / snag / deadwood / conifer (incl. hemlock) / broadleaf (oak, ash, sycamore,
    birch) / ore / stone (incl. rubble) / shrub / grass) whose substring occurs in the species (from
    the INS_<Species>_ component name) or mesh; else other, listed in ``uncategorised``. ``bands_m``
    defaults to [[-60,-20],[-20,-6],[-6,6],[6,20],[20,60]]. Stops at max_instances (``truncated``);
    reports the time."""
    return _run_prk_module("prk_lake_ops", "foliage_band_density", polyline=polyline, bands_m=bands_m,
                           categories=categories, max_instances=max_instances)


# --- Macro terrain authoring: terrain-relative relief, bead chains, views, scoped save ----------
# Editor half: Content/Python/prk_terrain_author.py (stamp writes go through prk_lake_ops' guarded
# stamp_configure / stamp_spawn / stamp_delete). Host half: helpers/prk_terrain_author_host.py (presets, k
# resolution, apply loop, capture polling, Perforce). Presets: Config/Editor/ZoneAuthoring/macro_presets.json;
# k_peak table: stamp_calibration.json. Docs/errant-macro-authoring-tools-2026-10-09.md.

_ERRANT_WORLD = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld"


def _host_result(fn, **kwargs) -> Dict[str, Any]:
    try:
        out = fn(**kwargs)
    except Exception as e:
        return {"success": False, "message": f"{type(e).__name__}: {e}"}
    out["success"] = bool(out.get("ok", out.get("success", False)))
    return out


@mcp.tool()
def errant_macro_presets() -> Dict[str, Any]:
    """Read-only, MCP server only: the bead-chain presets (woodland_hill, rocky_shoulder, dry_swale,
    mountain_pass, rolling_saddle) with each texture's k_peak resolved: the stamp_calibration.json entry (measured or
    estimated) first, else a thumbnail estimate (k = (max/255)^2.2; polarity peak/trough)."""
    return _host_result(prk_terrain_author_host.macro_presets)


@mcp.tool()
def errant_stamp_relief(
    actor: str,
    relief_m: Optional[float] = None,
    relief_delta_m: Optional[float] = None,
    relief_factor: Optional[float] = None,
    crest_above_ground_m: Optional[float] = None,
    depth_below_ground_m: Optional[float] = None,
    lift_m: Optional[float] = None,
    lift_delta_m: Optional[float] = None,
    k_peak: Optional[float] = None,
    measure: bool = False,
    allow_flip: bool = False,
    dry_run: bool = True,
    max_dirty_proxies: int = 12,
    expect_world: str = _ERRANT_WORLD,
) -> Dict[str, Any]:
    """Set an EXISTING stamp's height by its effect (writes; dry_run defaults True). Writes through
    errant_stamp_configure with all its rendering and editor-state guards.

    Height model: own relief = k_peak x multiplier x scale_z x 256 x landscape scale Z (negative when
    inverted). One relief target: ``relief_m`` (absolute signed m), ``relief_delta_m``, ``relief_factor`` (needs
    no k), ``crest_above_ground_m`` (Max / additive) or ``depth_below_ground_m`` (Min / additive), measured from
    the median ground on a ring at the footprint edge. One lift target (base offset above the landscape origin:
    actor Z for BrushZLocation, z_offset for Manual): ``lift_m`` or ``lift_delta_m``. k_peak defaults to the
    calibration table, else a thumbnail estimate. Refuses a raise<->dig flip unless ``allow_flip``, terrain
    targets on trough textures, and terrain targets while Errant edit mode is on (stale heights).
    ``measure=True`` also reports ring / inner heights. A real call from committed mode plans first, enters
    guarded edit mode, applies without remeasuring, waits for brush updates and exits with the camera
    preserved. Calls already in active mode retain it for batching. Returns plan, configure result, k_peak,
    source, and managed-cycle ``applied`` / ``committed`` status."""
    return _host_result(
        prk_terrain_author_host.stamp_relief, dispatch=_run_prk_module, actor=actor, k_peak=k_peak,
        expect_world=expect_world, relief_m=relief_m, relief_delta_m=relief_delta_m, relief_factor=relief_factor,
        crest_above_ground_m=crest_above_ground_m, depth_below_ground_m=depth_below_ground_m, lift_m=lift_m,
        lift_delta_m=lift_delta_m, measure=measure, allow_flip=allow_flip, dry_run=dry_run,
        max_dirty_proxies=max_dirty_proxies)


@mcp.tool()
def errant_feature_plan(
    name: str,
    preset: str,
    points: List[List[float]],
    width_m: Optional[float] = None,
    height_m: Optional[Any] = None,
    taper_m: Optional[float] = None,
    spacing_frac: Optional[float] = None,
    floor_z_m: Optional[List[float]] = None,
    descending: Optional[bool] = None,
    base_margin_m: float = 0.0,
    priority: Optional[int] = None,
    expect_world: str = _ERRANT_WORLD,
) -> Dict[str, Any]:
    """Plan a connected ridge or valley as a chain of square stamp beads along ``points`` ([[x, y], ...] cm).
    Read-only on the world; requires Errant mode off and the subsystem idle before tracing or rewriting
    Saved/TerrainAuthor/features/<name>.plan.json. A legitimate no-cut replan records an empty newest plan
    with ok=True, no_cut_needed=True and nothing_to_apply=True rather than leaving an older plan active.

    ``preset``: woodland_hill / rocky_shoulder (ridges, Max) or dry_swale / mountain_pass / rolling_saddle (valleys, Min);
    overrides: width_m, height_m (crest raise or relative cut depth; a number or one per control point), taper_m,
    spacing_frac, floor_z_m (absolute valley grade in metres per control point), descending, base_margin_m,
    priority. Explicit floor_z_m overrides relative height/taper/descending floor shaping on any valley preset;
    taper still controls end-bead placement and footprint falloff blends ends. Each bead's ground is traced
    (7x7 inside its falloff square); automatic crest / floor targets are terrain-relative, beads yaw 0/90/180/270,
    one priority strictly above every covering brush. Returns bead count, skipped beads, priority, crest/floor
    range, predicted proxies and advisory bank-slope warnings. Labels PRK_<name>_<nn>."""
    return _host_result(
        prk_terrain_author_host.feature_plan, dispatch=_run_prk_module, preset=preset, name=name, points=points,
        width_m=width_m, height_m=height_m, taper_m=taper_m, spacing_frac=spacing_frac, floor_z_m=floor_z_m,
        descending=descending, base_margin_m=base_margin_m, priority=priority, expect_world=expect_world)


@mcp.tool()
def errant_feature_apply(
    name: str,
    replace: bool = False,
    dry_run: bool = True,
    beads_per_call: int = 4,
    expect_world: str = _ERRANT_WORLD,
) -> Dict[str, Any]:
    """Spawn a planned chain (writes; dry_run defaults True) through errant_stamp_spawn, beads_per_call per
    editor call with 0.5 s host sleeps between. Every spawn guard applies (edit mode active, backup, proxy
    budget, rendering constraints). Existing beads whose class, texture, transform, height, falloff, offset and
    LOD settings match the plan are skipped (resume); different ones refuse unless
    ``replace=True``, which first deletes every PRK_ScriptedStamp bead PRK_<name>_*. Stops at the first failure.
    Keeps Saved/TerrainAuthor/features/<name>.applied.json (spawned / deleted packages and boxes).
    Returns spawned / deleted row arrays, skipped_existing label array and total planned count. A newest
    valid empty plan without replace returns ok=True / nothing_to_apply=True and preserves existing actors.
    Explicit replace=True retires old owned beads even for an empty valid plan and journals their deleted
    packages/boxes for scoped save. Empty unavailable-ground plans refuse without deleting anything."""
    return _host_result(
        prk_terrain_author_host.feature_apply, dispatch=_run_prk_module, name=name, replace=replace,
        dry_run=dry_run, beads_per_call=beads_per_call, expect_world=expect_world)


@mcp.tool()
def errant_feature_delete(
    name: str, dry_run: bool = True, expect_world: str = _ERRANT_WORLD,
) -> Dict[str, Any]:
    """Delete a chain's beads recorded in its applied journal (writes; dry_run defaults True) through
    errant_stamp_delete (tag + PRK_<name>_ prefix checked; edit mode active). Records their packages and
    boxes as deleted so errant_save_scoped(feature=name) saves them."""
    return _run_prk_module("prk_terrain_author", "feature_delete", name=name, dry_run=dry_run,
                           expect_world=expect_world)


@mcp.tool()
def errant_feature_calibrate(name: str, record: bool = False, expect_world: str = _ERRANT_WORLD) -> Dict[str, Any]:
    """With Errant edit mode OFF: trace each applied bead centre against its planned crest / floor ->
    per-bead and median ratio, centre_response_k (beads under 2 m planned relief skipped). These collision
    heights include overlapping brushes and miss off-centre texture peaks. ``record=True`` refuses to write
    this centre-only response into the global k_peak table; use ``record=False`` for the optional report."""
    return _run_prk_module("prk_terrain_author", "feature_calibrate", name=name, record=record,
                           expect_world=expect_world)


@mcp.tool()
def errant_view(
    action: str,
    name: str = "",
    label: str = "",
    settle_s: float = 2.0,
    timeout_s: float = 60.0,
    resolution_x: int = 1920,
    resolution_y: int = 1080,
    labels: Optional[List[str]] = None,
    location: Optional[List[float]] = None,
    rotation: Optional[List[float]] = None,
    fov: Optional[float] = None,
):
    """Independent named terrain capture cameras; no action moves the user's editor viewport.

    View names are case-insensitive and stored in lowercase; agents should choose different names.
    ``save`` records explicit ``location`` [x,y,z] cm and ``rotation`` [pitch,yaw,roll] degrees together,
    or reads the current editor camera when omitted. ``fov`` is horizontal degrees (default 90).
    ``capture`` renders the named pose through a per-request transient SceneCapture2D; explicit pose/FOV
    overrides affect only that capture and never reposition the editor camera. It waits host-side for an
    idle Errant subsystem with PIE stopped, shaders and settle_s, then verifies the complete PNG.
    ``restore`` returns the named capture pose without moving the editor camera; ``list`` shows poses/images.
    ``compare`` stitches two captures (latest two, or ``labels=[a,b]``). Capture and compare return images.
    New saves use individual atomic Saved/TerrainAuthor/views/<name>.json files; older views.json remains
    readable. Image filenames are unique across MCP processes. Only loaded World Partition cells render;
    agents have independent cameras but still share the editor's current terrain. Native CapturePerspective
    must be compiled/loaded; unavailable capture fails explicitly without using the editor viewport."""
    result = _host_result(
        prk_terrain_author_host.view, action=action, dispatch=_run_prk_module,
        wait_shaders=wait_for_shaders, name=name, label=label, settle_s=settle_s, timeout_s=timeout_s,
        resolution_x=resolution_x, resolution_y=resolution_y, labels=labels,
        location=location, rotation=rotation, fov=fov)
    image = result.get("compare") if action == "compare" else result.get("path") if action == "capture" else None
    if result.get("success") and image and Path(image).is_file() and Path(image).stat().st_size <= 32 * 1024 * 1024:
        return [result, Image(path=str(image), format="png")]
    return result


@mcp.tool()
def errant_save_scoped(
    stamps: Optional[List[str]] = None,
    feature: Optional[str] = None,
    include_proxies: Optional[List[str]] = None,
    extra_packages: Optional[List[str]] = None,
    checkout: bool = False,
    changelist: Optional[int] = None,
    dry_run: bool = True,
    expect_world: str = _ERRANT_WORLD,
) -> Dict[str, Any]:
    """Save exactly the packages a terrain edit owns (writes on dry_run=False; never Save All).

    Scope: the named ``stamps``' external actor packages, ``feature``'s journal (spawned and deleted beads),
    the dirty landscape proxies their current and pending old brush boxes reach. ``include_proxies`` takes
    loaded LandscapeStreamingProxy ACTOR LABELS, e.g. LandscapeStreamingProxy_7_6_0, not package paths.
    ``extra_packages`` takes ElBrushActor / LandscapeStreamingProxy or external PRKSettlementMarker package
    paths only. Successful direct stamp_configure writes retain old/new bounds until fully scoped-saved.
    A guarded edit-mode batch also retains actual newly dirty FinishEdit proxies beyond predicted bounds.
    These join the scope only when every successfully edited batch actor is explicitly named or covered by
    the feature journal; otherwise ``commit_scope_omissions`` is advisory and ordinary scope still works.
    Entry-baseline dirty proxies are not claimed by the batch. Pending direct deletions can be named by their
    unique stamp label or actor name; ``deleted_stamp_files`` reports actual file existence after saving.
    Lists foreign dirty
    packages and dirty proxies outside the scope
    (never saved). Perforce per owned dirty file: in the depot and not opened -> ``p4 edit -c changelist``
    (needs checkout=True and a numbered changelist, never default); opened for edit/add or new -> ok;
    otherwise refused. A real run edits exactly those files, saves through the editor (refuses PIE, edit mode
    active, read-only files), verifies foreign dirty packages stayed dirty, then reopens new
    files the editor opened for add in the default CL into ``changelist``. Dry run: scope, actions, refusals."""
    try:
        paint = _paint_module()
    except Exception as e:
        return {"success": False, "message": f"prk_landscape_paint unavailable: {type(e).__name__}: {e}"}
    return _host_result(
        prk_terrain_author_host.save_scoped, dispatch=_run_prk_module, p4=paint._p4, parse_fstat=paint.parse_fstat,
        stamps=stamps, feature=feature, include_proxies=include_proxies, extra_packages=extra_packages,
        checkout=checkout, changelist=changelist, dry_run=dry_run,
        expect_world=expect_world)


def _paint_module():
    """Content/Python/prk_landscape_paint, reloaded: its planning half (numpy, Pillow) runs HERE, in the MCP server,
    never on the editor game thread; its editor half is reached through _run_prk_module."""
    import importlib
    import sys
    path = os.path.join(prk_texture_preview.project_root() or "", "Content", "Python")
    if path not in sys.path:
        sys.path.insert(0, path)
    import prk_terrain_geom
    import prk_landscape_paint
    importlib.reload(prk_terrain_geom)
    return importlib.reload(prk_landscape_paint)


def _paint_editor(func: str, **kwargs) -> Dict[str, Any]:
    return _run_prk_module("prk_landscape_paint", func, **kwargs)


@mcp.tool()
def landscape_paint_import(
    ledger: Dict[str, str],
    prev_ledger: Dict[str, Optional[str]],
    target_cl: int,
    window: Optional[List[int]] = None,
    dry_run: bool = True,
    probe_points: int = 240,
    ring_px: int = 64,
    whole_landscape: bool = False,
    layer_textures: Optional[Dict[str, str]] = None,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld.ErrantWorld",
    require_exact_sum: bool = False,
) -> Dict[str, Any]:
    """Region-limited landscape weight import from the ledger PNGs (writes; dry_run defaults True).

    ``ledger``: layer -> new 8192^2 8-bit PNG (pixel p <-> world -1625600 + 400 p cm, [row = Y, col = X]);
    ``prev_ledger``: layer -> the PNG on the landscape now (None = never painted; today the current
    Saved/LandscapePaint/ZT1008_*_8192.png, Rev E). Each changed layer is written
    into the Paint edit layer over its changed-pixel bbox only (bridge SetLandscapePaintLayerWeights), so only the
    proxies under those rects (pixel // 508; ``proxies``) go dirty. The planning runs in the MCP server (numpy +
    Pillow in the server env: Plugins/UnrealMCP/Python pyproject.toml / uv.lock; `uv add numpy` if missing);
    the editor only reads / writes raw rect files through the bridge.

    Guards, all listed in a dry run (a write refuses if any fails): right world, PIE stopped, Paint edit layer and
    every ledger layer known, no change beyond vertex 8128, per weight-blend group the sum is NEVER WORSE than live
    (|planned - 255| <= |live - 255| at every pixel; pixels already off 255 are reported in preexisting_off_255,
    not failed; ``require_exact_sum=True`` demands exactly 255), the reads left every read proxy clean
    (packages_dirtied_by_read / read_left_packages_clean), a ledger-vs-live probe (>= 200 points in the window and on a ring_px ring
    outside it, all within 1/255 of the PREVIOUS ledger: catches hand paint since the last pass), a numbered
    target_cl. The merge lands on a later editor tick: verify in a LATER call (landscape_paint_verify), then
    landscape_paint_checkout, then landscape_paint_save. Never saves, never touches p4.

    ``whole_landscape=True`` (with ``layer_textures`` layer -> imported mask texture) is the OLD route,
    LandscapeImportWeightmapFromRenderTarget: all 256 proxies dirty."""
    if whole_landscape:
        if not layer_textures:
            return {"success": False, "refused": True, "error": "whole_landscape needs layer_textures"}
        res = _paint_editor("whole_landscape_import", layer_textures=layer_textures, dry_run=dry_run,
                            expect_world=expect_world)
        res.setdefault("note", "WHOLE-LANDSCAPE import: all 256 proxies dirty.")
        return res
    try:
        out = _paint_module().run_import(ledger=ledger, prev_ledger=prev_ledger, target_cl=target_cl, window=window,
                                         dry_run=dry_run, probe_points=probe_points, ring_px=ring_px,
                                         expect_world=expect_world, editor=_paint_editor,
                                         require_exact_sum=require_exact_sum)
    except Exception as e:
        return {"success": False, "message": f"{type(e).__name__}: {e}"}
    out["success"] = bool(out.get("ok"))
    return out


@mcp.tool()
def landscape_paint_verify(plan_file: str) -> Dict[str, Any]:
    """Re-read every rect a landscape_paint_import wrote (bridge GetLandscapePaintLayerWeights, Paint edit layer)
    and compare with the ledger within 1/255; records ``verified`` in the plan. Call it in a LATER call than the
    import (the merge lands on an editor tick). Read-only."""
    out = _paint_module().verify(plan_file=plan_file, editor=_paint_editor)
    out["success"] = bool(out.get("ok"))
    return out


@mcp.tool()
def landscape_paint_checkout(plan_file: str, target_cl: Optional[int] = None, dry_run: bool = True) -> Dict[str, Any]:
    """p4 edit / reopen EXACTLY the changed proxies' package files (the import's ``proxies_dirtied``) into
    target_cl (default: the plan's). `p4 -p localhost:1666 -u Eric -c PRK_Workspace` on those files only; never a
    `p4 change` spec edit, never the default CL. Files opened for add/delete, or outside the depot, are refused.
    Dry run (default): fstat and the planned edit / reopen / none per file."""
    out = _paint_module().checkout(plan_file=plan_file, target_cl=target_cl, dry_run=dry_run, editor=_paint_editor)
    out["success"] = bool(out.get("ok"))
    return out


@mcp.tool()
def landscape_paint_save(plan_file: str, dry_run: bool = True) -> Dict[str, Any]:
    """Save EXACTLY the proxies a landscape_paint_import dirtied (refused until the plan is verified and checked
    out; refused if any of their files is read-only). Never Save All."""
    out = _paint_module().save(plan_file=plan_file, dry_run=dry_run, editor=_paint_editor)
    out["success"] = bool(out.get("ok"))
    return out


@mcp.tool()
def texture_preview(asset_path: str, out_dir: Optional[str] = None) -> Dict[str, Any]:
    """Preview a texture WITHOUT the editor: runs in the MCP server only. Maps /Game/... (or
    /<Plugin>/... via the project's Plugins/*/Content) to its .uasset, extracts the embedded
    editor thumbnail, saves it as PNG in out_dir (default <temp>/texture_preview; the project
    tree is refused) and returns size, min / max / mean, centre mean (central 10 %), the bbox
    of pixels > 20, and the path. The safe replacement for live texture exports, which assert
    on 16-bit formats (editor-fatal 2026-10-07). Thumbnail values are display-encoded 8-bit:
    good for shape and polarity, not exact heights."""
    try:
        return prk_texture_preview.texture_preview(asset_path, out_dir)
    except Exception as e:
        return {"success": False, "message": f"{type(e).__name__}: {e}"}


@mcp.tool()
def errant_license_status() -> Dict[str, Any]:
    """Report which Errant plugin modules are loaded. NOTE: true license
    *validity* is not reflected and cannot be read from Python — the authoritative
    'not licensed' signal is a PERSISTENT_GENERATION_ERROR from
    ``errant_biomes_generate`` or an 'invalid license' Output-Log line. The key is
    entered via each plugin's editor UI."""
    return _run_errant("license_status")


# ===========================================================================
# PRK generic-reflection / import / asset / source-control wrappers.
#
# These promote the recurring run_python patterns (generic property/array/function
# access on ANY target, factory-pinned imports, registry-truth asset checks) to
# first-class Tier-1 wrappers. They dispatch a fixed run_python snippet that calls
# the editor-side helper modules (Content/Python/prk_mcp_ops.py + prk_errant.py),
# reload-safe so module edits land without an editor restart. The heavy lifting is
# the already-compiled UPRKEditorAIBridge + prk_unreal_helpers — no C++ build is
# needed to add a wrapper here; an MCP-server restart picks up the new @mcp.tool.
# ===========================================================================

def _run_prk_module(module: str, func: str, **kwargs) -> Dict[str, Any]:
    """Call ``<module>.<func>(**kwargs)`` inside the editor (reload-safe) and map
    its dict result onto the MCP ``success`` convention, preserving every field."""
    kw = ", ".join(f"{k}={_py_literal(v)}" for k, v in kwargs.items())
    code = (
        "import json, importlib\n"
        f"import {module} as _m\n"
        "_m = importlib.reload(_m)\n"
        f"res = _m.{func}({kw})\n"
        'print("<<<__MCP_RESULT__>>>")\n'
        "print(json.dumps(res))\n"
        'print("<<</__MCP_RESULT__>>>")\n'
    )
    # ExecuteFile's command-line parser can mistake flags inside a visual note for
    # a filename/argument list. Explicit statement execution treats all text as code.
    response = _editor_run_python(code, statement=True)
    marker = _extract_json_marker(response)
    if marker is None:
        return {
            "success": False,
            "message": f"No result from {module}.{func} — is the editor running and "
                       f"Content/Python/{module}.py importable?",
            "raw": response,
        }
    out = {"success": bool(marker.get("ok", marker.get("success", True)))}
    out.update(marker)
    return out


def _errant_brush_guard(classes: List[Any], actors: List[Any], source: str) -> Optional[Dict[str, Any]]:
    """Refusal for the generic spawn / delete tools when they would create or remove an Errant
    Landscape brush: class strings first (no editor call), then the editor-side
    prk_lake_ops.errant_brush_check (stamp classes by native parent, actors by label/name)."""
    hits = prk_python_guard.check_brush_class_text(classes)
    if not hits:
        res = _run_prk_module("prk_lake_ops", "errant_brush_check",
                              classes=[str(c) for c in classes if c], actors=[str(a) for a in actors if a])
        if not res.get("ok"):
            return {"success": False, "refused": True,
                    "message": f"{source}: could not verify that no Errant brush is involved "
                               f"({res.get('error') or res.get('message')}); refused."}
        if res.get("brush_classes"):
            hits.append(prk_python_guard.brush_tool_hit(f"Errant brush class {res['brush_classes']}"))
        if res.get("brush_actors"):
            hits.append(prk_python_guard.brush_tool_hit(f"Errant brush actor {res['brush_actors']}"))
    return prk_python_guard.refusal(hits, source)


def _run_prk(func: str, **kwargs) -> Dict[str, Any]:
    return _run_prk_module("prk_mcp_ops", func, **kwargs)


@mcp.tool()
def set_object_property(
    target: str,
    property_path: str,
    value: Any,
    transact: bool = True,
    save: bool = False,
) -> Dict[str, Any]:
    """Set ONE reflected property by path on ANY target, then read it back to verify.

    The generalization of set_dataasset_property beyond loaded assets — also targets
    level actors, blueprint CDOs, and editor/engine/game-instance/world subsystems.

    target grammar:
      "/Game/Foo/DA_Bar"             asset
      "actor:BP_Gate_2"              level actor by name or label (editor world)
      "cdo:/Game/AI/BP_Foo"          CDO of a blueprint's generated class
      "class:/Script/PRK.PRKPawn"    CDO of a native/generated class
      "subsystem:PRKBiomeMapSubsystem[@editor]"   subsystem (PIE world preferred; @editor forces editor)

    value is UE ImportText: "5", "True", "(X=1,Y=2,Z=3)"; a soft/hard object ref
    accepts a bare asset-path string. For arrays use set_array_property; for rich
    JSON values on a loaded asset, prefer set_dataasset_property.

    property_path may be dotted/indexed: "Stats.Strength", "FamilyRules[2].SoftTarget".
    Returns stored_value (read-back), saved.
    """
    refused = prk_python_guard.refusal(prk_python_guard.check_bridge_write(target, property_path),
                                       "set_object_property")
    if refused:
        return refused
    return _run_prk("set_object_property", target=target, property_path=property_path,
                    value=value, transact=transact, save=save)


@mcp.tool()
def get_object_property(target: str, property_path: str) -> Dict[str, Any]:
    """Read ONE reflected property by path (dotted/indexed) on ANY target.

    The read half of the set_object_property verify loop, generalized beyond assets.
    See set_object_property for the target grammar."""
    return _run_prk("get_object_property", target=target, property_path=property_path)


@mcp.tool()
def set_array_property(
    target: str,
    property_path: str,
    array_literal: str,
    transact: bool = True,
    save: bool = False,
) -> Dict[str, Any]:
    """Replace a whole array property in ONE write from a UE-text array literal.

    The corruption-free alternative to update_data_asset's per-element JSON
    round-trip (which can silently mangle complex structs / drop soft-class refs):
    a single ImportText of e.g. ((A="x"),(A="y")). Routes through UPRKEditorAIBridge.
    Returns the post-write array_length. See set_object_property for target grammar."""
    refused = prk_python_guard.refusal(prk_python_guard.check_bridge_write(target, property_path),
                                       "set_array_property")
    if refused:
        return refused
    return _run_prk("set_array_property", target=target, property_path=property_path,
                    array_literal=array_literal, transact=transact, save=save)


@mcp.tool()
def mhc_blend_face_region(
    character: str,
    gizmo_index: int,
    presets: List[str],
    weights: Optional[List[float]] = None,
    blend_mode: str = "Both",
    symmetric: bool = True,
    commit: bool = True,
) -> Dict[str, Any]:
    """MetaHuman Creator Blend tool, scripted: blend ONE face blend handle of a MetaHumanCharacter
    toward preset characters with the Creator's own solver, then commit the face state.

    character: MetaHumanCharacter asset path (opened for editing if needed; never saved).
    gizmo_index: handle index from mhc_face_blend_handles; -1 blends the whole face.
    presets: MetaHumanCharacter asset paths (e.g. /MetaHumanCharacter/Optional/Presets/Bruce);
        read from their saved face state, never opened for editing.
    weights: one per preset (default: even split); 1.0 on one preset = that preset's handle.
    blend_mode: Both | Proportions | Features. symmetric: blend the mirrored handle too.
    Routes through UPRKEditorAIBridge::MHCBlendFaceRegion (needs PRKEditor built)."""
    return _run_prk("mhc_blend_face_region", character=character, gizmo_index=gizmo_index,
                    presets=presets, weights=weights, blend_mode=blend_mode,
                    symmetric=symmetric, commit=commit)


@mcp.tool()
def mhc_face_blend_handles(character: str) -> Dict[str, Any]:
    """Positions of a MetaHumanCharacter's face blend handles (the Creator's Blend/Move
    manipulators) in mhc_blend_face_region's gizmo_index order, to tell which handle is the nose,
    a brow, a cheek or a jaw angle. Opens the character for editing if needed."""
    return _run_prk("mhc_face_blend_handles", character=character)


@mcp.tool()
def get_array_length(target: str, property_path: str) -> Dict[str, Any]:
    """Number of elements in an array property (path may address a nested array).
    -1 if the path is not an array. See set_object_property for target grammar."""
    return _run_prk("get_array_length", target=target, property_path=property_path)


@mcp.tool()
def invoke_function(
    target: str,
    function_name: str,
    args: Optional[List[Any]] = None,
) -> Dict[str, Any]:
    """Call any reflected (UFUNCTION) method on ANY target with text args.

    The only scriptable route to invoke a reflected method on a world subsystem or
    level actor. Args are imported into the function's parameters in declaration
    order; the return / out-params come back as text in `return`. Only
    BlueprintCallable / otherwise-reflected functions are reachable.

    See set_object_property for the target grammar (subsystem:/actor:/cdo:/asset)."""
    refused = prk_python_guard.refusal(prk_python_guard.check_bridge_write(target, function_name, args),
                                       "invoke_function")
    if refused:
        return refused
    return _run_prk("invoke_function", target=target, function_name=function_name,
                    args=args or [])


@mcp.tool()
def resolve_subsystem(class_name: str, prefer_pie: bool = True) -> Dict[str, Any]:
    """Confirm a subsystem resolves (editor / engine / game-instance / world) and
    report its class + object path. World/GI subsystems require UPRKEditorAIBridge.
    A pre-flight before set_object_property/invoke_function on a 'subsystem:' target."""
    return _run_prk("resolve_subsystem", class_name=class_name, prefer_pie=prefer_pie)


# --- Runtime (PIE-world) object lifecycle ----------------------------------
#
# The one surface every other tool misses: a RUNNING PIE world. place_actors_batch,
# find_actors_by_class, delete_actor and the "actor:" target all hit the editor
# world; these route through UPRKEditorAIBridge.GetTargetWorld(prefer_pie). Use them
# to drive a live PIE session — spawn a test actor, attach a component, enumerate
# what's actually spawned, tear it down.

@mcp.tool()
def spawn_actor(
    class_name: str,
    location: Optional[List[float]] = None,
    rotation: Optional[List[float]] = None,
    prefer_pie: bool = True,
) -> Dict[str, Any]:
    """Spawn an actor in the RUNNING (PIE-preferred) world — the runtime analogue of
    place_actors_batch, which only targets the editor world.

    class_name: /Script native path, short type name, BP asset path ("/Game/.../BP_X"
    → its generated class), or explicit "..._C" class path. location [x,y,z] /
    rotation [pitch,yaw,roll] default to origin. prefer_pie=False spawns into the open
    editor level instead. Bridge-backed. Returns the spawned actor's name/path/location."""
    refused = _errant_brush_guard([class_name], [], "spawn_actor")
    if refused:
        return refused
    return _run_prk("spawn_actor", class_name=class_name, location=location,
                    rotation=rotation, prefer_pie=prefer_pie)


@mcp.tool()
def add_component(
    target: str,
    component_class: str,
    attach_to_root: bool = True,
) -> Dict[str, Any]:
    """Add + register a component on a LIVE actor — the runtime analogue of
    add_blueprint_component (which only edits the Blueprint CDO/SCS, not a spawned
    instance).

    target is a resolvable actor: "actor:Name@pie" for a PIE actor, "actor:Name" for
    an editor-world actor. component_class: native path / short name / BP path.
    Scene components attach to the actor root. Bridge-backed. Returns the component's
    name/path/class."""
    return _run_prk("add_component", target=target, component_class=component_class,
                    attach_to_root=attach_to_root)


@mcp.tool()
def destroy_actor(target: str) -> Dict[str, Any]:
    """Destroy a live actor in its own world — reaches PIE actors that the editor-world
    delete_actor can't. target is a resolvable actor ("actor:Name@pie" for a PIE
    actor). Bridge-backed. Returns destroyed: bool."""
    refused = _errant_brush_guard([], [target], "destroy_actor")
    if refused:
        return refused
    return _run_prk("destroy_actor", target=target)


@mcp.tool()
def find_actors(
    class_path: str,
    prefer_pie: bool = True,
    limit: int = 200,
) -> Dict[str, Any]:
    """List actors of a class in the RUNNING (PIE-preferred) world — the PIE-aware
    find_actors_by_class. prefer_pie=False uses the editor world. class_path: short
    type name, BP path, or "/Script/..." class path. Bridge-backed. Returns
    name/label/path/class/location per actor."""
    return _run_prk("find_actors", class_path=class_path, prefer_pie=prefer_pie, limit=limit)


@mcp.tool()
def get_ai_snapshot(name_filter: str = "", prefer_pie: bool = True) -> Dict[str, Any]:
    """Full AI diagnostic dump for one pawn (or the fleet) in the RUNNING world —
    the headless answer to "what is this NPC doing and why".

    name_filter: case-insensitive substring of the pawn's instance name OR its
    CharacterName (e.g. "Brombert"). The bridge skips all per-row work for
    non-matching pawns, so a single-AI pull is cheap; an EMPTY filter snapshots
    the whole fleet (expensive in a populated town — prefer a filter).

    Each row: identity/provenance (role, faction, settlement, resident id),
    current/previous Athena task + age + TaskDebugState + lastFail/backoff,
    candidate task scores + per-consideration gating failures, live consideration
    values, combat/montage execution detail, nav goal + reachability + stall
    monitor, seat/POI claims, and computed dysfunction flags."""
    return _run_prk("ai_snapshot", name_filter=name_filter, prefer_pie=prefer_pie)


@mcp.tool()
def import_static_mesh(
    source_path: str,
    destination_path: str,
    replace_existing: bool = True,
    save: bool = True,
) -> Dict[str, Any]:
    """Import an FBX as a UStaticMesh, with the FbxFactory pinned to avoid the 5.7
    Interchange recursive-WaitUntilTasksComplete crash. destination_path is the full
    /Game/.../SM_Name (no extension). Returns the imported asset_path."""
    return _run_prk("import_static_mesh", source_path=source_path,
                    destination_path=destination_path,
                    replace_existing=replace_existing, save=save)


@mcp.tool()
def import_skeletal_mesh(
    source_path: str,
    destination_path: str,
    skeleton_path: Optional[str] = None,
    replace_existing: bool = True,
    save: bool = True,
) -> Dict[str, Any]:
    """Import an FBX as a USkeletalMesh (FbxFactory pinned), optionally bound to an
    existing Skeleton at skeleton_path. Returns the imported asset_path."""
    return _run_prk("import_skeletal_mesh", source_path=source_path,
                    destination_path=destination_path, skeleton_path=skeleton_path,
                    replace_existing=replace_existing, save=save)


@mcp.tool()
def import_sound_wave(
    source_path: str,
    destination_path: str,
    replace_existing: bool = True,
    save: bool = True,
) -> Dict[str, Any]:
    """Import a WAV as a USoundWave (SoundFactory pinned). Returns the asset_path."""
    return _run_prk("import_sound_wave", source_path=source_path,
                    destination_path=destination_path,
                    replace_existing=replace_existing, save=save)


@mcp.tool()
def import_asset_with_factory(
    source_path: str,
    destination_path: str,
    factory_class: str,
    replace_existing: bool = True,
    save: bool = True,
) -> Dict[str, Any]:
    """Generic import escape hatch — pin an explicitly named Factory subclass
    (e.g. "FbxFactory", "SoundFactory", "TextureFactory") so Interchange is never
    auto-chosen (the 5.7 crash). Returns the imported asset_path."""
    return _run_prk("import_asset_with_factory", source_path=source_path,
                    destination_path=destination_path, factory_class=factory_class,
                    replace_existing=replace_existing, save=save)


@mcp.tool()
def asset_exists(asset_path: str) -> Dict[str, Any]:
    """Registry-truth existence check. Use instead of does_asset_exist, which
    false-negatives on package-vs-object paths, BP-vs-_C class, redirectors, and
    mid-scan staleness. Returns exists: bool."""
    return _run_prk("asset_exists", asset_path=asset_path)


@mcp.tool()
def resolve_asset(asset_path: str) -> Dict[str, Any]:
    """Resolve an asset's canonical object_path (and asset_class) via the asset
    registry, defeating the load_asset/does_asset_exist false negatives. Returns
    exists: bool and, when present, object_path + asset_class."""
    return _run_prk("resolve_asset", asset_path=asset_path)


@mcp.tool()
def source_control_add(asset_path: str) -> Dict[str, Any]:
    """Mark a new/generated asset for add (P4) — the `p4 add` new assets need (not
    `p4 edit`). Best-effort: targets Content assets in the client view. For an
    existing asset that also needs saving under source control, prefer
    save_asset_checked. Returns the resolved file path."""
    return _run_prk("source_control_add", asset_path=asset_path)


# --- Canonical regional biome assignment (registration/save/generation separate) ---

@mcp.tool()
def errant_biome_register(
    biome_path: str, prk_biome_path: Optional[str] = None, dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Register an existing EB asset against the CURRENT live biome list.

    Appends without renumbering old entries. Optionally links a PRK biome DA
    without changing its persisted R index. Reports EB and PRK indices separately;
    neither is the canonical zone G ID. Dry-run defaults True. Returns exact
    owned-package receipt; save and generation are separate.
    """
    return _run_prk_module("prk_biome_regions", "register", biome_path=biome_path,
                           prk_biome_path=prk_biome_path, dry_run=dry_run, expect_world=expect_world)


@mcp.tool()
def errant_biome_region_probe(
    size: int = 255, eb_index: int = 10,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Check the regional render path using scratch targets only.

    Reuses the existing compiled copy material and initializes one retained
    blend master/MID. Requires exact selected and retained scratch values before
    native assignment is allowed. Does not write world masks, generate or save.
    Run with terrain mode off and no overlapping captures or render mutations.
    """
    return _run_prk_module("prk_biome_regions", "probe_render_path", size=size,
                           eb_index=eb_index, expect_world=expect_world)


@mcp.tool()
def errant_biome_assign_zone(
    zone_index: int, biome_path: str,
    zone_map_path: str = "/Game/PRK/Core/Data/World/DA_ZoneMap",
    expected_eb_index: Optional[int] = None, dry_run: bool = True,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Assign a registered EB asset to a canonical zone's complete G footprint.

    Includes detached pieces; writes native biome-mask RTs with binary selectors
    and preserves every unselected value. A conservative native-sample boundary
    margin retains old values where the sample domain crosses a neighbor. Full
    changed-tile readback verifies both assigned and preserved samples. Dry-run
    defaults True; scoped save and bounded generation remain separate. Does not
    alter landscape weights, foliage, water, canonical G or the PRK R map.
    """
    return _run_prk_module("prk_biome_regions", "assign_zone", zone_index=zone_index,
                           biome_path=biome_path, zone_map_path=zone_map_path,
                           expected_eb_index=expected_eb_index, dry_run=dry_run, expect_world=expect_world)


@mcp.tool()
def errant_biome_region_verify(
    receipt: str, expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Read receipt-owned complete native RT tiles, including preserved neighbors.

    Refuses changed canonical G or EB-list snapshots. Uses raw RT values rather
    than the biome-location sampler's 30-second cache. No paint or generation.
    """
    return _run_prk_module("prk_biome_regions", "verify", receipt=receipt, expect_world=expect_world)


@mcp.tool()
def errant_biome_region_save(
    receipt: str, checkout: bool = False, target_changelist: Optional[int] = None,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Verify and save only dirty packages owned by a biome-region receipt.

    Optional p4 edit targets precisely those existing files; foreign dirty
    packages remain dirty. No Save All, generation or map-wide biome bake.
    New native mask packages are saved; p4 add remains explicit.
    """
    return _run_prk_module("prk_biome_regions", "save", receipt=receipt, checkout=checkout,
                           target_changelist=target_changelist, expect_world=expect_world)


@mcp.tool()
def errant_biomes_generation_save_plan(
    component_xys: List[List[int]], species_paths: List[str], dirty_before: List[str],
    record: bool = False, expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Resolve dirty outputs of completed scoped biome generation; no render/save.

    Uses native component identities, runtime dependency references, explicit
    source species and the native PCG mask-neighbor ring. Preserves every exact
    pre-generation dirty package. Unknown new dirty outputs refuse save.
    record=True writes a JSON ownership receipt only, never Unreal content.
    """
    return _run_prk_module("prk_biome_generation_save", "plan", component_xys=component_xys,
                           species_paths=species_paths, dirty_before=dirty_before,
                           record=record, expect_world=expect_world)


@mcp.tool()
def errant_biomes_generation_save(
    receipt: str, checkout: bool = False, target_changelist: Optional[int] = None,
    expect_world: str = "/Game/PRK/World/ErrantWorld/ErrantWorld",
) -> Dict[str, Any]:
    """Re-resolve generation output identity and save only receipt-owned packages.

    No Save All. Keeps pre-generation dirty packages dirty, optionally checks
    out exact existing files. New generated files need explicit source-control add.
    """
    return _run_prk_module("prk_biome_generation_save", "save", receipt=receipt,
                           checkout=checkout, target_changelist=target_changelist,
                           expect_world=expect_world)


# --- Errant Biomes bridge-backed wrappers (prk_errant.py) ------------------

@mcp.tool()
def errant_biomes_species_dont_spawn(
    species_path: str,
    weightmap_names: List[str],
    enable: bool = True,
    save: bool = True,
    merge: bool = False,
) -> Dict[str, Any]:
    """Make a UBiomesSpecies stop spawning on landscape weightmap layer(s) (e.g.
    ["Path_Dirt"]) and/or an Errant Paths mask asset (pass its object path,
    e.g. "/Game/.../EM_RemoveFoliage") by enabling the Growth mask's don't-spawn
    branches. merge=False REPLACES the list and DISABLES the unsupplied source;
    merge=True ADDS to the species' existing names/mask (prefer it). Returns
    before/after base-node state. Bridge-backed (needs PRKEditor compiled).
    NEW weightmap names only reach extraction after an editor restart."""
    return _run_prk_module("prk_errant", "biomes_species_dont_spawn",
                           species_path=species_path, weightmap_names=weightmap_names,
                           enable=enable, save=save, merge=merge)


@mcp.tool()
def errant_biomes_species_dont_spawn_state(species_path: str) -> Dict[str, Any]:
    """Read a UBiomesSpecies' Growth-mask don't-spawn state: weightmap names,
    Errant Paths mask asset, and both static-switch defaults, from the BASE
    material nodes the generator reads. Bridge-backed."""
    return _run_prk_module("prk_errant", "biomes_species_dont_spawn_state",
                           species_path=species_path)


@mcp.tool()
def errant_biomes_paint_mask_box(
    mask_path: str,
    world_min: List[float],
    world_max: List[float],
    value: float = 1.0,
    dab_radius: float = 0.0,
    with_falloff: bool = False,
) -> Dict[str, Any]:
    """Stamp a world-space box into a hand-painted Biomes mask (e.g.
    EHM_Plains_NoSpawn) as a persistent "don't spawn" region — bakes a building/wall
    footprint so no foliage generates inside it (editor + runtime, zero runtime cost).
    XY is the footprint; Z only sets the dab plane. Does NOT save or regenerate —
    save the mask and regenerate biomes (errant_biomes_invalidate_cache first)."""
    return _run_prk_module("prk_errant", "biomes_paint_mask_box",
                           mask_path=mask_path, world_min=world_min, world_max=world_max,
                           value=value, dab_radius=dab_radius, with_falloff=with_falloff)


@mcp.tool()
def errant_biomes_species_min_spawn_z(
    species_path: str,
    min_world_z: Optional[float] = None,
    fade: float = 300.0,
    max_world_z: Optional[float] = None,
    max_fade: float = 500.0,
    enable: bool = True,
    save: bool = True,
) -> Dict[str, Any]:
    """Gate a UBiomesSpecies by a WORLD HEIGHT band (cm): nothing spawns below
    min_world_z (fading in over `fade` cm) or above max_world_z (fading out over
    `max_fade` cm ABOVE it — hard ceiling = max_world_z + max_fade, measured).
    EB has no altitude property, so this injects a
    HeightWithFade gate into the species' Growth-mask material (pure
    MaterialEditingLibrary — no bridge/build needed). A bound left None keeps what an
    existing gate already has (else unlimited on that side). Re-run rebuilds;
    enable=False removes the gate. Used for the sea-level/beach cutoff (ocean Z=1600,
    beach band to ~2960): vegetation min_world_z=2900 fade=300, rocks 1650 fade=100.
    Regenerate biomes — after errant_biomes_invalidate_cache — to see the effect."""
    return _run_prk_module("prk_errant", "biomes_species_min_spawn_z",
                           species_path=species_path, min_world_z=min_world_z,
                           fade=fade, max_world_z=max_world_z, max_fade=max_fade,
                           enable=enable, save=save)


@mcp.tool()
def errant_biomes_invalidate_cache() -> Dict[str, Any]:
    """Drop the persistent Biomes generation cache so the next regen re-reads edited
    masks/species. Call after errant_biomes_species_dont_spawn / paint_mask_box,
    before errant_biomes_generate. Bridge-backed."""
    return _run_prk_module("prk_errant", "biomes_invalidate_cache")


# --- Blueprint creation / generic asset management / level + nav lifecycle ---

@mcp.tool()
def create_blueprint_at_path(
    asset_path: str,
    parent_class: str = "Actor",
    save: bool = True,
) -> Dict[str, Any]:
    """Create a Blueprint at an EXACT /Game path — the crash-safe alternative to
    create_blueprint.

    The stock create_blueprint prepends /Game/Blueprints/ to the name, so a
    path-qualified name double-slashes the package and triggers a FATAL CreatePackage
    assert that TAKES THE EDITOR DOWN. This creates at the literal path via
    AssetTools.create_asset. Prefer this over create_blueprint.

    asset_path: full "/Game/.../BP_Name". parent_class: short type ("Actor", "PRKPawn"),
    a BP path (-> its generated class), or "/Script/PRK.PRKPawn". Returns asset_path."""
    return _run_prk("create_blueprint_at_path", asset_path=asset_path,
                    parent_class=parent_class, save=save)


@mcp.tool()
def duplicate_asset(source_path: str, dest_path: str, save: bool = True) -> Dict[str, Any]:
    """Duplicate ANY asset type (BP, mesh, material, level, ...) on disk. The generic
    form of duplicate_data_asset; safe with no level open. dest must not exist yet."""
    return _run_prk("duplicate_asset", source_path=source_path, dest_path=dest_path, save=save)


@mcp.tool()
def rename_asset(source_path: str, dest_path: str) -> Dict[str, Any]:
    """Rename/move ANY asset on disk (leaves a redirector — follow with
    fixup_redirectors). Generic form of rename_data_asset."""
    return _run_prk("rename_asset", source_path=source_path, dest_path=dest_path)


@mcp.tool()
def delete_asset(asset_path: str) -> Dict[str, Any]:
    """Delete ANY asset on disk. No-op (ok) if it does not exist. Generic form of
    delete_data_asset."""
    return _run_prk("delete_asset", asset_path=asset_path)


@mcp.tool()
def save_asset(asset_path: str, only_if_dirty: bool = False) -> Dict[str, Any]:
    """Save ANY loaded asset to disk. For read-only / P4 assets that may need
    check-out, prefer save_asset_checked."""
    return _run_prk("save_asset", asset_path=asset_path, only_if_dirty=only_if_dirty)


@mcp.tool()
def save_current_level() -> Dict[str, Any]:
    """Save the currently-open editor level — the 'SAVE THE LEVEL after capture' step
    that settlement capture/bake flows silently lose work without."""
    return _run_prk("save_current_level")


@mcp.tool()
def save_dirty_packages(save_maps: bool = True, save_content: bool = True) -> Dict[str, Any]:
    """Save all dirty packages (maps and/or content) — the catch-all save before a
    risky op or handoff."""
    return _run_prk("save_dirty_packages", save_maps=save_maps, save_content=save_content)


@mcp.tool()
def find_actors_by_class(class_path: str, limit: int = 200) -> Dict[str, Any]:
    """List level actors of a class (name / label / path). class_path: short type
    name ("EpPath"), a BP path, or "/Script/..." class path. The by-class companion to
    find_actors_by_name. Operates on the already-open editor world — never loads a
    level (load_level via MCP fatal-crashes heavy WP worlds)."""
    return _run_prk("find_actors_by_class", class_path=class_path, limit=limit)


@mcp.tool()
def rebuild_navigation() -> Dict[str, Any]:
    """Trigger a navmesh rebuild on the open editor world (RebuildNavigation). Pairs
    with get_navmesh_build_status. CAVEAT (in the result): under invoker-only / dynamic
    nav generation this can be a no-op until a Nav Invoker is present — verify coverage
    in PIE, don't trust the dispatch."""
    return _run_prk("rebuild_navigation")


# --- World line trace / ground-height sampling -----------------------------

@mcp.tool()
def line_trace(
    start: List[float],
    end: List[float],
    channel: str = "Visibility",
    trace_complex: bool = False,
    prefer_pie: bool = False,
) -> Dict[str, Any]:
    """Single line trace.

    Returns blocking_hit and, on a hit, impact_point / impact_normal / location /
    distance / hit_actor / hit_component. start and end are [x, y, z].

    channel: a trace channel ("Visibility", "Camera") or a collision OBJECT channel
    ("WorldStatic", "WorldDynamic", "Pawn", "PhysicsBody", "Vehicle", "Destructible").
    Mapping assumes the engine-default collision ordering.

    prefer_pie=False (default) traces the open EDITOR world — never loads a level.
    prefer_pie=True routes through UPRKEditorAIBridge.GetTargetWorld so it samples a
    RUNNING PIE world; while PIE is up the editor world is the wrong, un-simulated one.
    Needs the bridge compiled in (PRKEditor build)."""
    return _run_prk("line_trace", start=start, end=end, channel=channel,
                    trace_complex=trace_complex, prefer_pie=prefer_pie)


@mcp.tool()
def sample_landscape_layers(
    world_xy: Optional[List[float]] = None,
    start_xy: Optional[List[float]] = None,
    end_xy: Optional[List[float]] = None,
    samples: int = 11,
    editing_weightmap: bool = False,
    prefer_pie: bool = False,
) -> Dict[str, Any]:
    """Landscape paint probe: every weightmap layer on the landscape component under a
    point, with its weight there (0..1), plus the landscape height.

    Pass world_xy [x, y] for one point, or start_xy + end_xy for a cross-section of
    ``samples`` evenly spaced points (each row carries offset_cm), e.g. across a road.
    editing_weightmap=False (default) reads the FINAL merged weightmap, which is what
    renders and what Errant Biomes generation reads. Only loaded landscape proxies are
    visible. Read-only. Bridge-backed (UPRKEditorAIBridge.SampleLandscapeLayerWeights)."""
    return _run_prk("sample_landscape_layers", world_xy=world_xy, start_xy=start_xy,
                    end_xy=end_xy, samples=samples, editing_weightmap=editing_weightmap,
                    prefer_pie=prefer_pie)


@mcp.tool()
def sample_landscape_edit_layers(
    world_xy: List[float],
    prefer_pie: bool = False,
) -> Dict[str, Any]:
    """Per-edit-layer landscape paint probe at [x, y]: the FINAL merged weights plus, for
    every edit layer (name, guid, class, visible, weight_alpha, has_data), the weight of
    each paint layer that edit layer has allocated on the component there. Shows which
    edit layer a paint comes from. Procedural edit layers (brushes, splines) own no
    weightmaps; their paint appears only in the final weights. Read-only: no merge, no
    update request, no edit-layer selection change. Bridge-backed
    (UPRKEditorAIBridge.SampleLandscapeEditLayerWeights)."""
    return _run_prk("sample_landscape_edit_layers", world_xy=world_xy, prefer_pie=prefer_pie)


@mcp.tool()
def landscape_health_scan(max_report: int = 50, prefer_pie: bool = False) -> Dict[str, Any]:
    """Landscape health over LOADED components: no final weightmap allocations, allocation
    texture index/channel out of range, null LayerInfo, two allocations on one texture
    channel, null weightmap textures, target layers allocated on no loaded component; plus a
    World Partition HLOD staleness heuristic (landscape streaming proxy package newer than an
    overlapping HLOD actor's package, from actor descs, no loads). Lists capped at
    max_report. Read-only. Bridge-backed (UPRKEditorAIBridge.LandscapeHealthScan)."""
    return _run_prk("landscape_health_scan", max_report=max_report, prefer_pie=prefer_pie)


@mcp.tool()
def capture_top_down(
    center_xy: List[float],
    size_m: float = 100.0,
    resolution: int = 1024,
    out_path: Optional[str] = None,
    capture_source: str = "BaseColor",
) -> Dict[str, Any]:
    """Orthographic top-down PNG of the EDITOR world around [x, y] (size_m square): a transient,
    outliner-hidden scene capture renders once and is destroyed; no package is dirtied (checked
    before/after and reported). Default out_path is under <temp>/top_down, never the project
    tree. capture_source "BaseColor" (unlit albedo) or "FinalColor" (lit). Only loaded World
    Partition cells render. Columns +X, rows +Y, same as probe_heatmap. Bridge-backed
    (UPRKEditorAIBridge.CaptureTopDown)."""
    return _run_prk("capture_top_down", center_xy=center_xy, size_m=size_m, resolution=resolution,
                    out_path=out_path, capture_source=capture_source)


@mcp.tool()
def get_ground_height(
    x: float,
    y: float,
    channel: str = "WorldStatic",
    z_start: float = 200000.0,
    z_end: float = -200000.0,
    prefer_pie: bool = False,
) -> Dict[str, Any]:
    """Terrain-height query: a downward trace at (x, y) over the Z span, returning the
    ground Z (+ impact_point / impact_normal / hit_actor). The reliable replacement for
    the Z=0 fallback that is wrong on elevated worlds. Widen z_start/z_end if the world
    exceeds +/-2 km vertically. hit=False means nothing was struck in the span.

    prefer_pie=True samples the running PIE world via the bridge (use this when PIE is
    up — the editor-world trace hits the wrong, un-simulated world). Needs PRKEditor build."""
    return _run_prk("get_ground_height", x=x, y=y, channel=channel,
                    z_start=z_start, z_end=z_end, prefer_pie=prefer_pie)


# --- DataTable creation -----------------------------------------------------

@mcp.tool()
def create_data_table(row_struct_path: str, asset_path: str, save: bool = True) -> Dict[str, Any]:
    """Create an empty UDataTable with the given row struct. The creation step the
    existing read_data_table / write_data_table_rows tools (which only edit an existing
    table) don't cover.

    row_struct_path: short struct name ("PRKStonePlacementRow") or
    "/Script/Module.StructName" — the USTRUCT name WITHOUT the leading F.
    asset_path: full /Game/.../DT_Name. Then populate rows with write_data_table_rows."""
    return _run_prk("create_data_table", row_struct_path=row_struct_path,
                    asset_path=asset_path, save=save)


@mcp.tool()
def set_pcg_node_settings_property(
    graph_path: str,
    node_index: int,
    property_path: str,
    value_text: str,
    transact: bool = True,
    save: bool = False,
) -> Dict[str, Any]:
    """Set a property on a PCG node's settings sub-object via the C++ bridge.

    Python nativization cannot convert FSoftObjectPath, FGameplayTag, or other
    custom Errant/plugin struct types. This bypasses that by routing through
    UPRKEditorAIBridge::SetPropertyByPath → FProperty::ImportText_Direct, which
    handles any reflected property regardless of struct complexity.

    graph_path:    /Game/... path to the PCGGraph asset.
    node_index:    0-based index into graph.Nodes. Input=0, Output=1 are the fixed
                   terminals; user-added nodes start at 2. Confirm with get_pcg_graph_summary.
    property_path: reflected property name on the settings class,
                   e.g. "SpeciesAsset", "template_actor_class", "PointExtents".
    value_text:    UE ImportText format. SoftObjectPath example:
                   "/Game/.../ES_POI_BF_BanditCamp.ES_POI_BF_BanditCamp"
    """
    return _run_prk(
        "set_pcg_node_settings_property",
        graph_path=graph_path,
        node_index=node_index,
        property_path=property_path,
        value_text=value_text,
        transact=transact,
        save=save,
    )


# --- Zone recipe composition and resumable sculpting ----------------------

@mcp.tool()
def errant_zone_recipe_catalog(recipe_id: Optional[str] = None, details: bool = False) -> Dict[str, Any]:
    """Existing stamp recipes and measured reference settings, read in the MCP SERVER.

    Compositions: mountain gateway, ancient forest basin, pastoral valley, capped plateau.
    No editor round trip. details includes texture fingerprints and calibration provenance.
    New placements still require committed composite/width/collision and visual checks.
    """
    return prk_zone_catalog.catalog(recipe_id=recipe_id, details=details)


@mcp.tool()
def errant_zone_plan(zone_key: str) -> Dict[str, Any]:
    """Compile a registered zone brief/recipe plan into exact stamp arguments OFFLINE.

    Reads Config/Editor/ZoneAuthoring/pilot_zones.json and stamp_recipes.json.
    Checks numeric fields, unique actor IDs, cap ordering, batch and context budgets.
    Initial plans: High_Holds, Elder_Weald, Hearthlands. Does not contact the editor.
    """
    return prk_zone_catalog.compile_plan(zone_key)


@mcp.tool()
def errant_zone_inspect(zone_key: str) -> Dict[str, Any]:
    """Compact read-only zone brief, planned/present stamps, GUIDs and editor state.

    No terrain measurements, loading, generation, mode/camera changes or saves.
    """
    return _run_prk_module("prk_zone_authoring", "inspect_zone", zone_key=zone_key)


@mcp.tool()
def errant_zone_batch(
    zone_key: str, batch_id: str, run_id: str, thread_id: str,
    action: str = "preview", dry_run: bool = True,
    visual_note: Optional[str] = None, captures: Optional[Dict[str, str]] = None,
    accepted_packages: Optional[List[str]] = None, assessment_token: Optional[str] = None,
    fits: Optional[List[Dict[str, Any]]] = None,
    audit_rules: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """Resumable connected height-stamp batch, using the original guarded Errant tools.

    Stages (one call each): preview -> prepare -> apply -> commit -> verify -> accept -> save.
    status reads the journal. recover removes/restores ONLY this batch's recorded edits;
    follow with commit -> verify, which must prove the baseline restored before recovery save.
    dry_run=True always previews.
    prepare captures one bounded raw native-heightmap baseline; apply journals each intent/GUID and reconciles
    uncertain responses. Separate calls allow editor ticks and collision commit.
    verify checks useful connected crest width, relief and protected raw terrain, then collision separately.
    accept needs a concrete visual_note and entrance/squad/overview PNG paths from review_view.
    save writes only the checked manifest, refusing foreign/preexisting dirty packages.
    Completed saved/recovery_saved prepare/apply/apply_next/save retries validate current
    settings and package cleanliness, then return without reapplying, resaving or rewriting
    the immutable receipt. New dirty work or changed inputs require a new run ID.
    No biome generation, paint, roads, global bake, automation tests or save-all.
    """
    return _run_prk_module("prk_zone_authoring", "batch", zone_key=zone_key, batch_id=batch_id,
                           run_id=run_id, thread_id=thread_id, action=action, dry_run=dry_run,
                           visual_note=visual_note, captures=captures,
                           accepted_packages=accepted_packages, assessment_token=assessment_token,
                           fits=fits, audit_rules=audit_rules)


@mcp.tool()
def errant_zone_review_view(
    zone_key: str, batch_id: str, run_id: str, thread_id: str,
    view_id: Optional[str] = None, restore: bool = False,
) -> Dict[str, Any]:
    """Set entrance/squad/overview camera from committed raw landscape heights.

    Returns capture_path: use capture_viewport(file_path=...) in a LATER call, inspect its PNG,
    then supply those paths and visual_note to the batch accept stage. restore=True restores
    the camera recorded before this batch. Saved/recovery_saved reviews preserve the
    accepted journal and PNGs using a separate SHA-bound view-state sidecar and fresh
    post-save capture paths; restore then returns to the camera before that review cycle.
    No actors/assets are authored here.
    """
    return _run_prk_module("prk_zone_authoring", "review_view", zone_key=zone_key, batch_id=batch_id,
                           run_id=run_id, thread_id=thread_id, view_id=view_id, restore=restore)


@mcp.tool()
def errant_zone_run(
    zone_key: str, batch_id: str, run_id: str, thread_id: str,
    action: str = "review", dry_run: bool = True, visual_note: Optional[str] = None,
    accepted_packages: Optional[List[str]] = None, assessment_token: Optional[str] = None,
    tick_seconds: float = 1.0, wait_seconds: float = 30.0,
    fits: Optional[List[Dict[str, Any]]] = None,
    audit_rules: Optional[Dict[str, Any]] = None,
):
    """Batch runner: placement -> editor ticks -> collision commit -> measurements -> THREE IMAGES.

    review returns entrance/squad/overview PNGs, measurements, exact package manifest and
    assessment_token for review; it never accepts or saves automatically. accept_save needs
    that token, the exact accepted_packages and a concrete visual_note. Same run ID resumes
    after interruption without respawning completed GUIDs.
    dry_run=True previews. status reads the receipt; recover restores only unsaved owned edits.
    Completed acceptance/save retries check settings and cleanliness without new saves or
    receipt writes. Saved reviews return the existing accepted image bundle.
    Sleeps happen on the MCP host, allowing the editor to tick between separate Python calls.
    """
    result = prk_zone_runner.run(dispatch=_run_prk_module, capture=capture_viewport,
        zone_key=zone_key, batch_id=batch_id, run_id=run_id, thread_id=thread_id,
        action=action, dry_run=dry_run, visual_note=visual_note, accepted_packages=accepted_packages,
        assessment_token=assessment_token, tick_seconds=tick_seconds, wait_seconds=wait_seconds,
        fits=fits, audit_rules=audit_rules)
    result["success"] = bool(result.get("ok", result.get("success", False)))
    content = [result]
    for view in prk_zone_runner.VIEW_IDS:
        artifact = (result.get("review_bundle") or {}).get("captures", {}).get(view)
        if artifact:
            path = Path(artifact["path"])
            if path.is_file() and path.stat().st_size <= 32 * 1024 * 1024:
                content.extend([{ "view_id": view }, Image(path=str(path), format="png")])
    return content


@mcp.tool()
def errant_zone_fit(
    run_id: str, zone_key: str, batch_id: str, feature_id: str,
    target_delta_m: Optional[List[float]] = None,
    minimum_useful_width_m: Optional[List[float]] = None,
) -> Dict[str, Any]:
    """Fit amplitude AND connected usable width from a recorded measured library response OFFLINE.

    Returns exact scale/falloff fields and predicted connected width/relief; uses existing
    heightmaps only. Reads one frozen before/after journal; no surveys or editor contact.
    Holds XY footprint, yaw, source/mip/blend fixed. Refuses overlapping or clipped absolute
    measurements, noise, changed source files, and unsupported offsets. A prediction still
    needs a new live committed composite check and the three review views before acceptance.
    """
    return prk_zone_analysis.run("fit_run", run_id=run_id, zone_key=zone_key, batch_id=batch_id,
        feature_id=feature_id, target_delta_m=target_delta_m,
        minimum_useful_width_m=minimum_useful_width_m)


@mcp.tool()
def errant_zone_audit(
    run_id: str, zone_key: str, batch_id: str, rules: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """Reuse ONE committed survey to mark slope/wall, low-pass, drainage/join and shoreline problems.

    Returns exact world XY markers, a north-up SVG and GeoJSON under the same journal folder.
    No editor calls or repeated surveys. Default rules read analysis_settings.json. Supply
    passes(start/end, ceiling, slope, minimum_width, detour), drainage polylines and joins
    for travel/outlet checks. Explicit allowed cliff/shore/sink disks preserve intentional
    geography. Reports survey resolution and unmeasured/outside-grid routes as problems.
    """
    return prk_zone_analysis.run("audit_run", run_id=run_id, zone_key=zone_key, batch_id=batch_id, rules=rules)


@mcp.tool()
def errant_zone_cleanup(
    cleanup_id: str, thread_id: str, action: str = "preview", dry_run: bool = True,
    ownership_batches: Optional[List[Dict[str, str]]] = None,
    stamp_guids: Optional[List[str]] = None, custom_assets: Optional[List[str]] = None, cell_m: float = 4.0,
) -> Dict[str, Any]:
    """Exact-GUID batch cleanup. run handles ticks, collision, checks, exact saves and unused assets.

    First preview an explicit stamp_guids list and ownership_batches [{run_id,zone_key,batch_id}].
    run with dry_run=False removes ONLY those journal-owned scripted stamps, commits, verifies
    protected terrain/collision, and saves exact affected external-actor/proxy packages.
    custom_assets is an explicit optional list under /Game/PRK/World/ErrantWorld/Terrain/;
    library/catalog sources are protected. It checks ALL hard/soft/management and live brush
    references, archives exact files with verified hashes, then deletes only unused listed assets
    after actor deletion is persisted. No directory deletion, purge, bake or save-all. Same
    cleanup_id resumes partial results. status and individual stages are also available.
    """
    if action == "run":
        result = prk_zone_runner.run_cleanup(dispatch=_run_prk_module, cleanup_id=cleanup_id,
            thread_id=thread_id, ownership_batches=ownership_batches, stamp_guids=stamp_guids,
            custom_assets=custom_assets, cell_m=cell_m, dry_run=dry_run)
        result["success"] = bool(result.get("ok", False))
        return result
    return _run_prk_module("prk_zone_cleanup", "cleanup", cleanup_id=cleanup_id,
        thread_id=thread_id, action=action, dry_run=dry_run, ownership_batches=ownership_batches,
        stamp_guids=stamp_guids, custom_assets=custom_assets, cell_m=cell_m)


# Run the server
if __name__ == "__main__":
    logger.info("Starting Advanced MCP server with stdio transport")
    mcp.run(transport='stdio')

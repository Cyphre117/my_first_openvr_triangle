#include <SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL_opengl.h>
#include <openvr.h>

#include <cstdio>
#include <string>

// This is a tech test of loading up all the OpenVR things and putting something on the HMD
// Tested on Windows 10 with Visual Studio 2013 community and an Oculus DK2.
// Requires Oculus Home and Steam VR be installed
// Depends on SDL2, GLEW, GLM, and OpenVR
// 
// Oculus Home will try to convince you the DK2 is old and unsupported but it still works as of 2016-12-06
//
// This is based on the code from the hellovr_opengl sample included with OpenVR

/* Global variables */

// SDL2
SDL_Window* companion_window = nullptr;
const int companion_width = 1280;
const int companion_height = 640;
SDL_GLContext gl_context = 0;

// OpenGL 
GLuint scene_shader_program = 0;
GLuint scene_vao = 0;	// Vertex attribute object, stores the vertex layout
GLuint scene_vbo = 0;	// Vertex buffer object, stores the vertex data
GLint scene_matrix_location = -1;
GLuint window_shader_program = 0;
GLuint window_vao = 0;	// Vertex attribute object
GLuint window_vbo = 0;	// Vertex buffer object
GLuint window_ebo = 0;	// element buffer object, the order for vertices to be drawn

struct FrameBufferDesc
{
	GLuint depth_buffer;
	GLuint render_texture;
	GLuint render_frame_buffer;
	GLuint resolve_texture;
	GLuint resolve_frame_buffer;
} left_eye_desc, right_eye_desc;

// OpenvR
vr::IVRSystem* hmd = nullptr;
const float near_plane = 0.1f;
const float far_plane = 20.0f;
uint32_t hmd_render_target_width;
uint32_t hmd_render_target_height;
glm::mat4 left_eye_projection = glm::mat4( 1.0f );
glm::mat4 left_eye_to_pose = glm::mat4( 1.0f );
glm::mat4 right_eye_projection = glm::mat4( 1.0f );
glm::mat4 right_eye_to_pose = glm::mat4( 1.0f );

vr::TrackedDevicePose_t tracked_device_pose[vr::k_unMaxTrackedDeviceCount];
glm::mat4 mat4_device_pose[vr::k_unMaxTrackedDeviceCount];
std::string pose_classes_string;							// what classes we saw poses for this frame
char dev_class_char[vr::k_unMaxTrackedDeviceCount];			// for each device, a character representing its class
int valid_pose_count;
glm::mat4 hmd_pose_matrix;

/* Functions */

// Usefull for getting information about the current hardware setup
std::string GetTrackedDeviceString( vr::IVRSystem *pHmd, vr::TrackedDeviceIndex_t unDevice, vr::TrackedDeviceProperty prop, vr::TrackedPropertyError *peError = NULL )
{
	uint32_t unRequiredBufferLen = pHmd->GetStringTrackedDeviceProperty( unDevice, prop, NULL, 0, peError );
	if( unRequiredBufferLen == 0 )
		return "";

	char *pchBuffer = new char[unRequiredBufferLen];
	unRequiredBufferLen = pHmd->GetStringTrackedDeviceProperty( unDevice, prop, pchBuffer, unRequiredBufferLen, peError );
	std::string sResult = pchBuffer;
	delete[] pchBuffer;
	return sResult;
}

// Create a frame buffer for use with the HMD
// Fills in the Frame Buffer Description 
bool CreateFrameBuffer( int width, int height, FrameBufferDesc& desc )
{
	// render buffer
	glGenFramebuffers( 1, &desc.render_frame_buffer );
	glBindFramebuffer( GL_FRAMEBUFFER, desc.render_frame_buffer );

	// depth
	glGenRenderbuffers( 1, &desc.depth_buffer );
	glBindRenderbuffer( GL_RENDERBUFFER, desc.depth_buffer );
	glRenderbufferStorageMultisample( GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT, width, height );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, desc.depth_buffer );

	// texture
	glGenTextures( 1, &desc.render_texture );
	glBindTexture( GL_TEXTURE_2D_MULTISAMPLE, desc.render_texture );
	glTexImage2DMultisample( GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, width, height, true );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, desc.render_texture, 0 );

	// resolve buffer
	glGenFramebuffers( 1, &desc.resolve_frame_buffer );
	glBindFramebuffer( GL_FRAMEBUFFER, desc.resolve_frame_buffer );

	// texture
	glGenTextures( 1, &desc.resolve_texture );
	glBindTexture( GL_TEXTURE_2D, desc.resolve_texture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, desc.resolve_texture, 0 );

	// Check everything went ok
	if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
	{
		printf( "Error creating frame buffer!\n" );
		return false;
	}

	// Unbind any bound frame buffer
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	return true;
}

// Compile a shader program from two strings
// name is provided for prettier error messages
GLuint CreateShaderProgram( const char* name, const char* vertex_source, const char* fragment_source )
{
	GLuint shader_program = glCreateProgram();

	// Vertex shader
	GLuint vertex_shader = glCreateShader( GL_VERTEX_SHADER );
	glShaderSource( vertex_shader, 1, &vertex_source, NULL );
	glCompileShader( vertex_shader );

	GLint status = GL_FALSE;
	glGetShaderiv( vertex_shader, GL_COMPILE_STATUS, &status );
	if( status != GL_TRUE )
	{
		printf( "Failed to compile %s vertex shader\n", name );
		glDeleteProgram( shader_program );
		glDeleteShader( vertex_shader );
		return 0;
	}
	else
	{
		glAttachShader( shader_program, vertex_shader );
		glDeleteShader( vertex_shader ); // We can throw it away not it's been attached
	}

	// Fragment shader
	GLuint fragment_shader = glCreateShader( GL_FRAGMENT_SHADER );
	glShaderSource( fragment_shader, 1, &fragment_source, NULL );
	glCompileShader( fragment_shader );

	status = GL_FALSE;
	glGetShaderiv( fragment_shader, GL_COMPILE_STATUS, &status );
	if( status != GL_TRUE )
	{
		printf( "Failed to compile %s fragment shader\n", name );
		glDeleteProgram( shader_program );
		glDeleteShader( fragment_shader );
		return 0;
	}
	else
	{
		glAttachShader( shader_program, fragment_shader );
		glDeleteShader( fragment_shader );
	}

	// Now link the shaders into the program
	glLinkProgram( shader_program );
	status = GL_TRUE;
	glGetProgramiv( shader_program, GL_LINK_STATUS, &status );
	if( status != GL_TRUE )
	{
		printf( "Failed to link %s program\n", name );
		glDeleteProgram( shader_program );
		return 0;
	}
	else
	{
		printf( "Shader success! %d\n", shader_program );
	}

	glUseProgram( 0 );
	return shader_program;
}

glm::mat4 ConvertHMDMat4ToGLMMat4( const vr::HmdMatrix44_t& mat )
{
	return glm::mat4(
		mat.m[0][0], mat.m[1][0], mat.m[2][0], mat.m[3][0],
		mat.m[0][1], mat.m[1][1], mat.m[2][1], mat.m[3][1],
		mat.m[0][2], mat.m[1][2], mat.m[2][2], mat.m[3][2],
		mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3]
		);
}

glm::mat4 ConvertHMDMat3ToGLMMat4( const vr::HmdMatrix34_t& mat )
{
	return glm::mat4(
		mat.m[0][0], mat.m[1][0], mat.m[2][0], 0.0,
		mat.m[0][1], mat.m[1][1], mat.m[2][1], 0.0,
		mat.m[0][2], mat.m[1][2], mat.m[2][2], 0.0,
		mat.m[0][3], mat.m[1][3], mat.m[2][3], 1.0f
		);
}

glm::mat4 GetHMDMartixProjection( vr::Hmd_Eye eye )
{
	if( !hmd )
		return glm::mat4();

	vr::HmdMatrix44_t matrix = hmd->GetProjectionMatrix( eye, near_plane, far_plane, vr::API_OpenGL );

	return ConvertHMDMat4ToGLMMat4( matrix );
}

glm::mat4 GetHMDMatrixPoseEye( vr::Hmd_Eye eye )
{
	if( !hmd )
		return glm::mat4();

	vr::HmdMatrix34_t matrix = hmd->GetEyeToHeadTransform( eye );

	return ConvertHMDMat3ToGLMMat4( matrix );
}

void RenderScene( vr::Hmd_Eye eye )
{
	glm::mat4 view_proj_matrix = glm::mat4( 1.0 );

	//view_proj_matrix = glm::perspective( glm::radians( 45.0f ), companion_width / (float)companion_height, near_plane, far_plane )
	//	* glm::lookAt( glm::vec3( 2, 2, 2 ), glm::vec3( 0, 0, 0 ), glm::vec3( 0, 0, 1 ) );
	//view_proj_matrix = GetHMDMartixProjection( eye )
		//* glm::lookAt( glm::vec3( 2, 2, 2 ), glm::vec3( 0, 0, 0 ), glm::vec3( 0, 0, 1 ) )
		//* GetHMDMatrixPoseEye( eye )
		//* hmd_pose_matrix;

	if( eye == vr::Eye_Left )
	{
		// Left eye
		view_proj_matrix =
			left_eye_projection
			* left_eye_to_pose
			* hmd_pose_matrix;
	}
	else
	{
		// Right Eye
		view_proj_matrix =
			right_eye_projection
			* right_eye_to_pose
			* hmd_pose_matrix;
	}

	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	glUseProgram( scene_shader_program );
	glBindVertexArray( scene_vao );
	glBindBuffer( GL_ARRAY_BUFFER, scene_vbo );
	glUniformMatrix4fv( scene_matrix_location, 1, GL_FALSE, glm::value_ptr( view_proj_matrix ) );

	glDrawArrays( GL_TRIANGLES, 0, 12 );
}

void UpdateHMDMatrixPose()
{
	if( !hmd )
		return;

	vr::VRCompositor()->WaitGetPoses( tracked_device_pose, vr::k_unMaxTrackedDeviceCount, NULL, 0 );

	valid_pose_count = 0;
	pose_classes_string = "";
	for( int nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice )
	{
		if( tracked_device_pose[nDevice].bPoseIsValid )
		{
			valid_pose_count++;
			mat4_device_pose[nDevice] = ConvertHMDMat3ToGLMMat4( tracked_device_pose[nDevice].mDeviceToAbsoluteTracking );
			if( dev_class_char[nDevice] == 0 )
			{
				switch( hmd->GetTrackedDeviceClass( nDevice ) )
				{
				case vr::TrackedDeviceClass_Controller:        dev_class_char[nDevice] = 'C'; break;
				case vr::TrackedDeviceClass_HMD:               dev_class_char[nDevice] = 'H'; break;
				case vr::TrackedDeviceClass_Invalid:           dev_class_char[nDevice] = 'I'; break;
				case vr::TrackedDeviceClass_Other:             dev_class_char[nDevice] = 'O'; break;
				case vr::TrackedDeviceClass_TrackingReference: dev_class_char[nDevice] = 'T'; break;
				default:                                       dev_class_char[nDevice] = '?'; break;
				}
			}
			pose_classes_string += dev_class_char[nDevice];
		}
	}

	if( tracked_device_pose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid )
	{
		hmd_pose_matrix = mat4_device_pose[vr::k_unTrackedDeviceIndex_Hmd];
		hmd_pose_matrix = glm::inverse( hmd_pose_matrix );
	}
}

int main(int argc, char* argv[])
{
	if( SDL_Init( SDL_INIT_EVERYTHING ) < 0 )
	{
		printf( "Could not init SDL! Error: %s\n", SDL_GetError() );
		return 1;
	}

	{
		// We can call these before we load the runtime
		bool is_hmd_present = vr::VR_IsHmdPresent();
		bool is_runtime_installed = vr::VR_IsRuntimeInstalled();
		printf( "Found HMD: %s\n", is_hmd_present ? "yes" : "no" );
		printf( "Found OpenVR runtime: %s\n", is_runtime_installed ? "yes" : "no" );

		if( is_hmd_present == false || is_runtime_installed == false )
		{
			SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "error", "Something is missing...", NULL );
			return 1;
		}
	}

	// Load the OpenVR/SteamVR Runtime
	vr::EVRInitError init_error = vr::VRInitError_None;
	hmd = vr::VR_Init( &init_error, vr::VRApplication_Scene );
	if( init_error != vr::VRInitError_None )
	{
		hmd = nullptr;
		char buf[1024];
		sprintf_s( buf, sizeof( buf ), "Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription( init_error ) );
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "VR_Init Failed", buf, NULL );
		return 1;
	}

	(vr::IVRRenderModels *)vr::VR_GetGenericInterface( vr::IVRRenderModels_Version, &init_error );

	// Create the window
	companion_window = SDL_CreateWindow(
		"Hello VR",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		companion_width,
		companion_height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN );
	if( companion_window == NULL )
	{
		return 1;
	}

	// Setup the OpenGL Context
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 4 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 1 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
	SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );

	gl_context = SDL_GL_CreateContext( companion_window );
	if( gl_context == NULL )
	{
		return 1;
	}

	// Setup GLEW
	glewExperimental = GL_TRUE;
	if( glewInit() != GLEW_OK )
	{
		return 1;
	}

	{
		// Get some more info about our environment
		std::string driver = "No Driver";
		std::string display = "No Display";

		driver = GetTrackedDeviceString( hmd, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_TrackingSystemName_String );
		display = GetTrackedDeviceString( hmd, vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SerialNumber_String );
	
		printf( "Device: %s\n", display.c_str() );
		printf( "Driver: %s\n", driver.c_str() );
	}

	// Setup OpenGL
	{
		// Create Shaders
		const char* scene_vertex_source =
			"#version 410\n"
			"uniform mat4 matrix;"
			"in vec3 vPosition;"
			"void main()"
			"{"
			"	gl_Position = matrix * vec4(vPosition, 1.0);"
			"}";
		const char* scene_fragment_source =
			"#version 410\n"
			"out vec4 outColour;"
			"void main()"
			"{"
			"	outColour = vec4(1.0, 1.0, 1.0, 1.0);"
			"}";
		scene_shader_program = CreateShaderProgram( "scene", scene_vertex_source, scene_fragment_source );
		scene_matrix_location = glGetUniformLocation( scene_shader_program, "matrix" );

		const char* window_vertex_source =
			"#version 410\n"
			"in vec2 vPosition;"
			"in vec2 vUV;"
			"out vec2 fUV;"
			"void main()"
			"{"
			"	fUV = vUV;"
			"	gl_Position = vec4(vPosition, 0.0, 1.0);"
			"}";
		const char* window_fragment_source =
			"#version 410\n"
			"uniform sampler2D tex;"
			"in vec2 fUV;"
			"out vec4 outColour;"
			"void main()"
			"{"
			"	outColour = texture(tex, fUV);"
			"}";
		window_shader_program = CreateShaderProgram( "window", window_vertex_source, window_fragment_source );
	}

	// Setup the companion window data
	{
		// x, y,	u, v
		// x and y are in normalised device coordinates
		// each side should take up half the screen
		GLfloat verts[] =
		{
			// Left side
			-1.0, -1.0f,	0.0, 0.0,
			0.0, -1.0,		1.0, 0.0,
			-1.0, 1.0,		0.0, 1.0,
			0.0, 1.0,		1.0, 1.0,

			// Right side
			0.0, -1.0,		0.0, 0.0,
			1.0, -1.0,		1.0, 0.0,
			0.0, 1.0,		0.0, 1.0,
			1.0, 1.0,		1.0, 1.0
		};

		GLushort indices[] = { 0, 1, 3, 0, 3, 2, 4, 5, 7, 4, 7, 6 };

		glGenVertexArrays( 1, &window_vao );
		glBindVertexArray( window_vao );

		glGenBuffers( 1, &window_vbo );
		glBindBuffer( GL_ARRAY_BUFFER, window_vbo );
		glBufferData( GL_ARRAY_BUFFER, sizeof( verts ), verts, GL_STATIC_DRAW );

		glGenBuffers( 1, &window_ebo );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, window_ebo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( indices ), indices, GL_STATIC_DRAW );

		GLint posAttrib = glGetAttribLocation( window_shader_program, "vPosition" );
		glEnableVertexAttribArray( posAttrib );
		glVertexAttribPointer( posAttrib, 3, GL_FLOAT, GL_FALSE, 4 * sizeof( GLfloat ), 0 );

		GLint uvAttrib = glGetAttribLocation( window_shader_program, "vUV" );
		glEnableVertexAttribArray( uvAttrib );
		glVertexAttribPointer( uvAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof( GLfloat ), (void*)(2 * sizeof( GLfloat )) );
	}

	// Setup scene data
	{
		// Some hardcoded Triangles
		float vertices[] = {
			0.0f, 0.5f, 2.0f,
			0.5f, -0.5f, 2.0f,
			-0.5f, -0.5f, 2.0f,

			0.0f, 0.5f, -2.0f,
			0.5f, -0.5f, -2.0f,
			-0.5f, -0.5f, -2.0f,

			2, 0, 0,
			2, 1, 0,
			2, 1, 1,

			-2, 0, 0,
			-2, 1, 0,
			-2, 1, 1
		};

		// Create a crappy triangle for rendering
		glUseProgram( scene_shader_program );

		glGenVertexArrays( 1, &scene_vao );
		glBindVertexArray( scene_vao );

		glGenBuffers( 1, &scene_vbo ); // Generate 1 buffer
		glBindBuffer( GL_ARRAY_BUFFER, scene_vbo );
		glBufferData( GL_ARRAY_BUFFER, sizeof( vertices ), vertices, GL_STATIC_DRAW );

		GLint posAttrib = glGetAttribLocation( scene_shader_program, "vPosition" );
		glEnableVertexAttribArray( posAttrib );
		glVertexAttribPointer( posAttrib, 3, GL_FLOAT, GL_FALSE, 0, 0 );
	}

	// Setup the left and right render targets
	{
		hmd->GetRecommendedRenderTargetSize( &hmd_render_target_width, &hmd_render_target_height );

		CreateFrameBuffer( hmd_render_target_width, hmd_render_target_height, left_eye_desc );
		CreateFrameBuffer( hmd_render_target_width, hmd_render_target_height, right_eye_desc );
	}

	// Setup the compositer
	if( !vr::VRCompositor() )
	{
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "VR_Init Failed", "Could not initialise compositor", NULL );
		return 1;
	}

	// Grab the projection and eye to pos matrices
	// These are fixed, only the pose changes each frame
	left_eye_projection = GetHMDMartixProjection( vr::Eye_Left );
	left_eye_to_pose = GetHMDMatrixPoseEye( vr::Eye_Left );
	right_eye_projection = GetHMDMartixProjection( vr::Eye_Right );
	right_eye_to_pose = GetHMDMatrixPoseEye( vr::Eye_Right );
	
	// Finally!
	// The application loop
	bool done = false;
	SDL_Event sdl_event;
	vr::VREvent_t vr_event;
	while( !done )
	{
		// Process SDL events
		while( SDL_PollEvent( &sdl_event ) )
		{
			if( sdl_event.type == SDL_QUIT ) done = true;
			else if( sdl_event.type == SDL_KEYDOWN )
			{
				if( sdl_event.key.keysym.scancode == SDL_SCANCODE_ESCAPE ) done = true;
			}
		}

		// Process SteamVR events
		while( hmd->PollNextEvent( &vr_event, sizeof( vr_event ) ) )
		{
			// Apparently it works when we don't actulally poll the SteamVR event list, but you probably should
		}

		// HEY YOU - IMPORTANT!
		//
		// This must be called or the app will not gain focus! Currently called inside UpdateHMDMatrixPose();
		//		vr::TrackedDevicePose_t pose_buffer[vr::k_unMaxTrackedDeviceCount];
		//		vr::VRCompositor()->WaitGetPoses( pose_buffer, vr::k_unMaxTrackedDeviceCount, NULL, 0 );
		//
		// TBH at this stage I don't fully know what poses are,
		// apart from the fact valve seem to think they are important and we must get them
		// Something to do with the position of the HMD
		UpdateHMDMatrixPose();

		glEnable( GL_DEPTH_TEST );
		glClearColor( 0.0, 0.0, 0.0, 1.0 );

		// render left eye
		glEnable( GL_MULTISAMPLE );
		glBindFramebuffer( GL_FRAMEBUFFER, left_eye_desc.render_frame_buffer );
		glViewport( 0, 0, hmd_render_target_width, hmd_render_target_height );

		RenderScene( vr::Eye_Left );

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glDisable( GL_MULTISAMPLE );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, left_eye_desc.render_frame_buffer );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, left_eye_desc.resolve_frame_buffer );

		glBlitFramebuffer( 0, 0, hmd_render_target_width, hmd_render_target_height, 0, 0, hmd_render_target_width, hmd_render_target_height,
			GL_COLOR_BUFFER_BIT,
			GL_LINEAR );

		glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );

		// render Right eye
		glEnable( GL_MULTISAMPLE );
		glBindFramebuffer( GL_FRAMEBUFFER, right_eye_desc.render_frame_buffer );
		glViewport( 0, 0, hmd_render_target_width, hmd_render_target_height );

		RenderScene( vr::Eye_Right );

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glDisable( GL_MULTISAMPLE );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, right_eye_desc.render_frame_buffer );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, right_eye_desc.resolve_frame_buffer );

		glBlitFramebuffer( 0, 0, hmd_render_target_width, hmd_render_target_height, 0, 0, hmd_render_target_width, hmd_render_target_height,
			GL_COLOR_BUFFER_BIT,
			GL_LINEAR );

		glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );

		// Submit frames to HMD
		if( !hmd->IsInputFocusCapturedByAnotherProcess() )
		{
			vr::EVRCompositorError submit_error = vr::VRCompositorError_None;

			vr::Texture_t left_eye_texture = { (void*)left_eye_desc.resolve_texture, vr::API_OpenGL, vr::ColorSpace_Gamma };
			submit_error = vr::VRCompositor()->Submit( vr::Eye_Left, &left_eye_texture );
			if( submit_error != vr::VRCompositorError_None )
			{
				printf( "Error in left eye %d\n", submit_error );
			}


			vr::Texture_t right_eye_texture = { (void*)right_eye_desc.resolve_texture, vr::API_OpenGL, vr::ColorSpace_Gamma };
			submit_error = vr::VRCompositor()->Submit( vr::Eye_Right, &right_eye_texture );
			if( submit_error != vr::VRCompositorError_None )
			{
				printf( "Error in right eye\n" );
			}
		}
		else
		{
			printf( "Another process has focus of the HMD!\n" );
		}

		// Draw the companion window
		glDisable( GL_DEPTH_TEST );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glViewport( 0, 0, companion_width, companion_height );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		glBindVertexArray( window_vao );
		glUseProgram( window_shader_program );

		// render left eye (first half of index array )
		glBindTexture( GL_TEXTURE_2D, left_eye_desc.resolve_texture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0 );

		// render right eye (second half of index array )
		glBindTexture( GL_TEXTURE_2D, right_eye_desc.resolve_texture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glDrawElements( GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *)(12) );

		SDL_GL_SwapWindow( companion_window );
	}

	// Shutdown everything
	vr::VR_Shutdown();
	if( companion_window )
	{
		SDL_DestroyWindow( companion_window );
		companion_window = nullptr;
	}
	SDL_Quit();

	return 0;
}
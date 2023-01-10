/**
   Example for the ESP32_SC_W6100 HTTP(S) Webserver

   IMPORTANT NOTE:
   To run this script, your need to
    1) Make sure to have certificate data available. You will find a
       shell script (create_cert.sh) and instructions to do so in the library folder
       under extras/

   This script will install an HTTPS Server on your ESP32_SC_W6100 with the following
   functionalities:
    - Show simple page on web server root
    - Provide some "internal pages" that are protected by the server
    - Run a middleware that authenticates the user
    - Run a middleware that provides access control
    - 404 for everything else
   Authentication is done using HTTP Basic Auth, which is supported by the webserver,
   so you don't have to care about retrieving the login information from request
   headers.
*/

// Include certificate data (see note above)
#include "cert.h"
#include "private_key.h"


//////////////////////////////////////////////////

// For ESP32_SC_W6100
#define DEBUG_ETHERNET_WEBSERVER_PORT       Serial

// Debug Level from 0 to 4
#define _ETHERNET_WEBSERVER_LOGLEVEL_       3

//////////////////////////////////////////////////////////

// For ESP32-S3
// Optional values to override default settings
// Don't change unless you know what you're doing
//#define ETH_SPI_HOST        SPI3_HOST
//#define SPI_CLOCK_MHZ       25

// Must connect INT to GPIOxx or not working
//#define INT_GPIO            4

//#define MISO_GPIO           13
//#define MOSI_GPIO           11
//#define SCK_GPIO            12
//#define CS_GPIO             10

// For ESP32_C3
// Optional values to override default settings
// Don't change unless you know what you're doing
//#define ETH_SPI_HOST        SPI2_HOST
//#define SPI_CLOCK_MHZ       25

// Must connect INT to GPIOxx or not working
//#define INT_GPIO            10

//#define MISO_GPIO           5
//#define MOSI_GPIO           6
//#define SCK_GPIO            4
//#define CS_GPIO             7

//////////////////////////////////////////////////////////

#include <WebServer_ESP32_SC_W6100.h>

//////////////////////////////////////////////////

// Enter a MAC address and IP address for your controller below.
#define NUMBER_OF_MAC      20

byte mac[][NUMBER_OF_MAC] =
{
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x02 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x03 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x04 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x05 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x06 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x07 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x08 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x09 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x0A },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x0B },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x0C },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x0D },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x0E },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x0F },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x10 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x11 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x12 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x13 },
  { 0xDE, 0xAD, 0xBE, 0xEF, 0xBE, 0x14 },
};

// Select the IP address according to your local network
IPAddress myIP(192, 168, 2, 232);
IPAddress myGW(192, 168, 2, 1);
IPAddress mySN(255, 255, 255, 0);

// Google DNS Server IP
IPAddress myDNS(8, 8, 8, 8);

//////////////////////////////////////////////////

// We define two new HTTP-Header names. Those headers will be used internally
// to store the user name and group after authentication. If the client provides
// these headers, they will be ignored to prevent authentication bypass.
#define HEADER_USERNAME "X-USERNAME"
#define HEADER_GROUP    "X-GROUP"

#include <HTTPS_Server_Generic.h>

// The HTTPS Server comes in a separate namespace. For easier use, include it here.
using namespace httpsserver;

// Create an SSL certificate object from the files included above
SSLCert cert = SSLCert(
                 example_crt_DER, example_crt_DER_len,
                 example_key_DER, example_key_DER_len
               );

// Create an SSL-enabled server that uses the certificate
// The contstructor takes some more parameters, but we go for default values here.
HTTPSServer secureServer = HTTPSServer(&cert);

// Declare a middleware function.
// Parameters:
// req: Request data, can be used to access URL, HTTP Method, Headers, ...
// res: Response data, can be used to access HTTP Status, Headers, ...
// next: This function is used to pass control down the chain. If you have done your work
//       with the request object, you may decide if you want to process the request.
//       If you do so, you call the next() function, and the next middleware function (if
//       there is any) or the actual requestHandler will be called.
//       If you want to skip the request, you do not call next, and set for example status
//       code 403 on the response to show that the user is not allowed to access a specific
//       resource.
//       For more details, see the definition below.

/**
   The following middleware function is one of two functions dealing with access control. The
   middlewareAuthentication() will interpret the HTTP Basic Auth header, check usernames and password,
   and if they are valid, set the X-USERNAME and X-GROUP header.

   If they are invalid, the X-USERNAME and X-GROUP header will be unset. This is important because
   otherwise the client may manipulate those internal headers.

   Having that done, further middleware functions and the request handler functions will be able to just
   use req->getHeader("X-USERNAME") to find out if the user is logged in correctly.

   Furthermore, if the user supplies credentials and they are invalid, he will receive an 401 response
   without any other functions being called.
*/
void middlewareAuthentication(HTTPRequest * req, HTTPResponse * res, std::function<void()> next)
{
  // Unset both headers to discard any value from the client
  // This prevents authentication bypass by a client that just sets X-USERNAME
  req->setHeader(HEADER_USERNAME, "");
  req->setHeader(HEADER_GROUP, "");

  // Get login information from request
  // If you use HTTP Basic Auth, you can retrieve the values from the request.
  // The return values will be empty strings if the user did not provide any data,
  // or if the format of the Authorization header is invalid (eg. no Basic Method
  // for Authorization, or an invalid Base64 token)
  std::string reqUsername = req->getBasicAuthUser();
  std::string reqPassword = req->getBasicAuthPassword();

  // If the user entered login information, we will check it
  if (reqUsername.length() > 0 && reqPassword.length() > 0)
  {

    // _Very_ simple hardcoded user database to check credentials and assign the group
    bool authValid = true;
    std::string group = "";

    if (reqUsername == "admin" && reqPassword == "secret")
    {
      group = "ADMIN";
    }
    else if (reqUsername == "user" && reqPassword == "test")
    {
      group = "USER";
    }
    else
    {
      authValid = false;
    }

    // If authentication was successful
    if (authValid)
    {
      // set custom headers and delegate control
      req->setHeader(HEADER_USERNAME, reqUsername);
      req->setHeader(HEADER_GROUP, group);

      // The user tried to authenticate and was successful
      // -> We proceed with this request.
      next();
    }
    else
    {
      // Display error page
      res->setStatusCode(401);
      res->setStatusText("Unauthorized");
      res->setHeader("Content-Type", "text/plain");

      // This should trigger the browser user/password dialog, and it will tell
      // the client how it can authenticate
      res->setHeader("WWW-Authenticate", "Basic realm=\"ESP32 privileged area\"");

      // Small error text on the response document. In a real-world scenario, you
      // shouldn't display the login information on this page, of course ;-)
      res->println("401. Unauthorized (try admin/secret or user/test)");

      // NO CALL TO next() here, as the authentication failed.
      // -> The code above did handle the request already.
    }
  }
  else
  {
    // No attempt to authenticate
    // -> Let the request pass through by calling next()
    next();
  }
}

/**
   This function plays together with the middlewareAuthentication(). While the first function checks the
   username/password combination and stores it in the request, this function makes use of this information
   to allow or deny access.

   This example only prevents unauthorized access to every ResourceNode stored under an /internal/... path.
*/
void middlewareAuthorization(HTTPRequest * req, HTTPResponse * res, std::function<void()> next)
{
  // Get the username (if any)
  std::string username = req->getHeader(HEADER_USERNAME);

  // Check that only logged-in users may get to the internal area (All URLs starting with /internal)
  // Only a simple example, more complicated configuration is up to you.
  if (username == "" && req->getRequestString().substr(0, 9) == "/internal")
  {
    // Same as the deny-part in middlewareAuthentication()
    res->setStatusCode(401);
    res->setStatusText("Unauthorized");
    res->setHeader("Content-Type", "text/plain");
    res->setHeader("WWW-Authenticate", "Basic realm=\"ESP32 privileged area\"");
    res->println("401. Unauthorized (try admin/secret or user/test)");

    // No call denies access to protected handler function.
  }
  else
  {
    // Everything else will be allowed, so we call next()
    next();
  }
}

// This is the internal page. It will greet the user with
// a personalized message and - if the user is in the ADMIN group -
// provide a link to the admin interface.
void handleInternalPage(HTTPRequest * req, HTTPResponse * res)
{
  // Header
  res->setStatusCode(200);
  res->setStatusText("OK");
  res->setHeader("Content-Type", "text/html; charset=utf8");

  // Write page
  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head>");
  res->println("<title>Internal Area</title>");
  res->println("</head>");
  res->println("<body>");

  // Personalized greeting
  res->print("<h1>Hello ");
  // We can safely use the header value, this area is only accessible if it's
  // set (the middleware takes care of this)
  res->printStd(req->getHeader(HEADER_USERNAME));
  res->print("!</h1>");

  res->println("<p>Welcome to the internal area. Congratulations on successfully entering your password!</p>");

  // The "admin area" will only be shown if the correct group has been assigned in the authenticationMiddleware
  if (req->getHeader(HEADER_GROUP) == "ADMIN")
  {
    res->println("<div style=\"border:1px solid red;margin: 20px auto;padding:10px;background:#ff8080\">");
    res->println("<h2>You are an administrator</h2>");
    res->println("<p>You are allowed to access the admin page:</p>");
    res->println("<p><a href=\"/internal/admin\">Go to secret admin page</a></p>");
    res->println("</div>");
  }

  // Link to the root page
  res->println("<p><a href=\"/\">Go back home</a></p>");
  res->println("</body>");
  res->println("</html>");
}

void handleAdminPage(HTTPRequest * req, HTTPResponse * res)
{
  // Headers
  res->setHeader("Content-Type", "text/html; charset=utf8");

  std::string header =
    "<!DOCTYPE html><html><head><title>Secret Admin Page</title></head><body><h1>Secret Admin Page</h1>";
  std::string footer = "</body></html>";

  // Checking permissions can not only be done centrally in the middleware function but also in the actual request handler.
  // This would be handy if you provide an API with lists of resources, but access rights are defined object-based.
  if (req->getHeader(HEADER_GROUP) == "ADMIN")
  {
    res->setStatusCode(200);
    res->setStatusText("OK");
    res->printStd(header);
    res->println("<div style=\"border:1px solid red;margin: 20px auto;padding:10px;background:#ff8080\">");
    res->println("<h1>Congratulations</h1>");
    res->println("<p>You found the secret administrator page!</p>");
    res->println("<p><a href=\"/internal\">Go back</a></p>");
    res->println("</div>");
  }
  else
  {
    res->printStd(header);
    res->setStatusCode(403);
    res->setStatusText("Unauthorized");
    res->println("<p><strong>403 Unauthorized</strong> You have no power here!</p>");
  }

  res->printStd(footer);
}

// Just a simple page for demonstration, very similar to the root page.
void handlePublicPage(HTTPRequest * req, HTTPResponse * res)
{
  res->setHeader("Content-Type", "text/html");
  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head><title>Hello World!</title></head>");
  res->println("<body>");
  res->println("<h1>Hello World!</h1>");
  res->print("<p>Your server is running for ");
  res->print((int)(millis() / 1000), DEC);
  res->println(" seconds.</p>");
  res->println("<p><a href=\"/\">Go back</a></p>");
  res->println("</body>");
  res->println("</html>");
}

// For details on the implementation of the hanlder functions, refer to the Static-Page example.
void handleRoot(HTTPRequest * req, HTTPResponse * res)
{
  res->setHeader("Content-Type", "text/html");
  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head><title>Hello World!</title></head>");
  res->println("<body>");
  res->println("<h1>Hello World!</h1>");
  res->println("<p>This is the authentication and authorization example. When asked for login "
               "information, try admin/secret or user/test.</p>");
  res->println("<p>Go to: <a href=\"/internal\">Internal Page</a> | <a href=\"/public\">Public Page</a></p>");
  res->println("</body>");
  res->println("</html>");
}

void handle404(HTTPRequest * req, HTTPResponse * res)
{
  req->discardRequestBody();
  res->setStatusCode(404);
  res->setStatusText("Not Found");
  res->setHeader("Content-Type", "text/html");
  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head><title>Not Found</title></head>");
  res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
  res->println("</html>");
}

void setup()
{
  // For logging
  Serial.begin(115200);

  while (!Serial && millis() < 5000);

  delay(500);

  ///////////////////////////////////////////////

  Serial.print("\nStarting Authentication on " + String(ARDUINO_BOARD));
  Serial.println(" with " + String(SHIELD_TYPE));
  Serial.println(WEBSERVER_ESP32_SC_W6100_VERSION);
  Serial.println(HTTPS_SERVER_GENERIC_VERSION);

  ///////////////////////////////////

  // To be called before ETH.begin()
  ESP32_W6100_onEvent();

  // start the ethernet connection and the server:
  // Use DHCP dynamic IP and random mac
  //bool begin(int MISO_GPIO, int MOSI_GPIO, int SCLK_GPIO, int CS_GPIO, int INT_GPIO, int SPI_CLOCK_MHZ,
  //           int SPI_HOST, uint8_t *W6100_Mac = W6100_Default_Mac);
  ETH.begin( MISO_GPIO, MOSI_GPIO, SCK_GPIO, CS_GPIO, INT_GPIO, SPI_CLOCK_MHZ, ETH_SPI_HOST );
  //ETH.begin( MISO_GPIO, MOSI_GPIO, SCK_GPIO, CS_GPIO, INT_GPIO, SPI_CLOCK_MHZ, ETH_SPI_HOST, mac[millis() % NUMBER_OF_MAC] );

  // Static IP, leave without this line to get IP via DHCP
  //bool config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1 = 0, IPAddress dns2 = 0);
  //ETH.config(myIP, myGW, mySN, myDNS);

  ESP32_W6100_waitForConnect();

  ///////////////////////////////////

  Serial.print(F("HTTPS EthernetWebServer is @ IP : "));
  Serial.println(ETH.localIP());

  Serial.print(F("To access, use https://"));
  Serial.println(ETH.localIP());

  ///////////////////////////////////////////////

  // For every resource available on the server, we need to create a ResourceNode
  // The ResourceNode links URL and HTTP method to a handler function
  ResourceNode * nodeRoot     = new ResourceNode("/", "GET", &handleRoot);
  ResourceNode * nodeInternal = new ResourceNode("/internal", "GET", &handleInternalPage);
  ResourceNode * nodeAdmin    = new ResourceNode("/internal/admin", "GET", &handleAdminPage);
  ResourceNode * nodePublic   = new ResourceNode("/public", "GET", &handlePublicPage);
  ResourceNode * node404      = new ResourceNode("", "GET", &handle404);

  // Add the nodes to the server
  secureServer.registerNode(nodeRoot);
  secureServer.registerNode(nodeInternal);
  secureServer.registerNode(nodeAdmin);
  secureServer.registerNode(nodePublic);

  // Add the 404 not found node to the server.
  // The path is ignored for the default node.
  secureServer.setDefaultNode(node404);

  // Add the middleware. These functions will be called globally for every request
  // Note: The functions are called in the order they are added to the server.
  // This means, we need to add the authentication middleware first, because the
  // authorization middleware needs the headers that will be set by the authentication
  // middleware (First we check the identity, then we see what the user is allowed to do)
  secureServer.addMiddleware(&middlewareAuthentication);
  secureServer.addMiddleware(&middlewareAuthorization);

  Serial.println("Starting server...");
  secureServer.start();

  if (secureServer.isRunning())
  {
    Serial.println("Server ready.");
  }
}

void loop()
{
  // This call will let the server do its work
  secureServer.loop();

  // Other code would go here...
  delay(1);
}
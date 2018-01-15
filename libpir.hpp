#ifndef DEF_LIBPIR
#define DEF_LIBPIR

#include <pir/shared_queue.hpp>
#include <pir/PIRParameters.hpp>
#include <pir/queryGen/PIRQueryGenerator_internal.hpp>
#include <pir/replyExtraction/PIRReplyExtraction_internal.hpp>
#include <pir/replyGenerator/PIRReplyGeneratorNFL_internal.hpp>
#include <crypto/NFLLWE.hpp>
#include <crypto/HomomorphicCryptoFactory_internal.hpp>
#include <crypto/HomomorphicCrypto.hpp>
#include "pir/dbhandlers/DBGenerator.hpp"
#include "pir/dbhandlers/DBDirectoryProcessor.hpp"
#include "pir/dbhandlers/DBVectorProcessor.hpp"
#include <stdint.h>



/**
 * Class storing the database (or a chunk of it) after pre-processing
 * Type returned by the funcion importData below
**/
class imported_database : public imported_database_t
{
public:
  ~imported_database();
};



/** 
 *	HomomorphicCryptoFactory is used to create a generic cryptographic object (Ring-LWE, Paillier, 
 *	mockup-cryptography, etc.). This API only exposes the Ring-LWE cryptosystem, but we still 
 *	use the generic factory to get an instance of this cryptosystem in order to avoid code duplication.
**/
class HomomorphicCryptoFactory : public HomomorphicCryptoFactory_internal
{
public:
  /** prints a list of the available crypto parameters 
  *   (CryptoSystem:SecurityMax:PolyDegree:ModulusBitsize)
  **/
  static void printAllCryptoParams();
  
  /** takes one of the parameters given by the previous 
  *   method as a string and returns an instance of a cryptosystem for the given parameters  
  **/
  static HomomorphicCrypto* getCryptoMethod(std::string cryptoParams);
};




/** 
 *	PIRQueryGenerator is Client side, it initiates the PIR protocol by generating a query
 *	corresponding to the chosen element
**/
class PIRQueryGenerator : public PIRQueryGenerator_internal
{
public:

  /**
  *	Class constructor
  *	Params:
  *		- PIRParameters& pirParameters_ 	: PIRParameters structure defining the cryptographic
  *		  parameters, aggregation, recursion, and the shape of the database, see the main() 
  *		  function of apps/simplepir/simplePIR.cpp for detailed usage examples
  *		- NFLLWE& cryptoMethod_ 			: shared_pointer of the cryptographic instance
  **/
  PIRQueryGenerator(PIRParameters& pirParameters, HomomorphicCrypto& cryptoMethod_);
  
  /**
   * Generates asynchronously queries (a set of encryptions of 0 or 1).
   * Params:
   *   - uint64_t _chosenElement : index of the element to be retrieved (indexes start at 0)
   * Can be called on a separate thread.
  **/
  void generateQuery(uint64_t _chosenElement );

  /** 
   * Function to get a pointer to a char* query element.
   * Returns false when the queue is over (true otherwise) and waits when it's empty
   * Can be called on a separate thread.
  **/
  bool popQuery(char** query);

  /**
   * Get the size in bytes of a query element
  **/ 
  uint64_t getQueryElementBytesize();

private:
  int nbQueries;
};



/** 
 *	PIRReplyGenerator is Server side
 *	It handles the request generated by the client and generates the reply
**/
class PIRReplyGenerator : public PIRReplyGeneratorNFL_internal
{
public:

  /**
   *	Constructor of the class.
   *	Params :
   *	- vector <std::string>& database : reference of vector of the database elements
   *	- PIRParameters& param : reference to a PIRParameters structure (see the PIRQueryGenerator 
   *	  constructor help for more details).
   *	- DBHandler* db : abstract type for a database handler, at the moment it can be a DBGenerator
   *	  that generates a fake database or a DBDirectoryProcessor that works on real files. See the
   *	  main() function of apps/simplepir/simplePIR.cpp for examples.
  **/
  PIRReplyGenerator(PIRParameters& param, HomomorphicCrypto& cryptoMethod_, DBHandler *db);

  /**
   *  Feeds the Server with the queries, this includes a pre-computation phase that speeds up
   *  reply generation (computation of newton coefficients, see the associated paper)
   *  Can be called on a separate thread but all the query elements must be pushed before 
   *  starting the reply generation.
  **/		
  void pushQuery(char* rawQuery);
	
  /**
   *  Imports the database into a usable form. If the database is too large to fit in RAM
   *  (there is an expansion factor of 3 due to pre-processing) it can be cut by reading
   *  only for each element a chunk of size bytes_per_db_element starting at a given offset
   *  Can be called on a separate thread but the database must be imported before starting 
   *  the reply generation.
  **/
  imported_database* importData(uint64_t offset, uint64_t bytes_per_db_element);

  /**
   *  Prepares reply and start absoptions, returns number of chunks. 
   *  Precondition: database is precomputed
   *
   *  The PIRParameters given to the constructor are used and therefore the reply will
   *  be potentially generated with recursion and aggregation 
   *  Can be called on a separate thread but the database must be imported and the query pushed
   *  before starting the reply generation.
  **/
  void generateReply(const imported_database* database);
 
  /** 
   *  Frees the queries allocated 
  **/ 
  void freeQueries();
	
  /** 
   *  Gets a pointer to a char* reply element.
   *  Returns false when the queue is over (true otherwise) and waits when it's empty
   *  Can be called on a separate thread
  **/
  bool popReply(char** reply);

  /** 
   *  Getter for nbRepliesGenerated, the amount or reply elements generated 
  **/
  uint64_t getnbRepliesGenerated();

  /**
   *  Gets the size in bytes of a reply element
  **/ 
  uint64_t getReplyElementBytesize();

private:
	uint64_t nbRepliesToHandle, nbRepliesGenerated, currentReply;
};



/** 
 *	PIRReplyExtraction is Client side, it extracts the chosen element from the reply of the Server
**/
class PIRReplyExtraction : public PIRReplyExtraction_internal
{
public :

  /**
   *  Class constructor
   *  Params:
   *  - PIRParameters& pirParameters : reference to a PIRParameters structure (see the
   *  	PIRQueryGenerator constructor help for more details).
   *  - NFLLWE& cryptoMethod_ 			: shared_pointer of the cryptographic instance.
  **/
  PIRReplyExtraction(PIRParameters& pirParameters, HomomorphicCrypto& cryptoMethod);
	
  /**
   *  Can be called on a separate thread
   *  Feed the client with the reply, may wait if the internal queue is full until
   *  enough replies are decrypted. If such thing happens extractReply should be called
   *  on a different thread
  **/			
  void pushEncryptedReply(char* rawBytes);
	
  /**
   *  Can be called on a separate thread
   *  extract/decrypt the result from the pushed replies, the plaintext results have to be popped.
   *  May block if the internal queue of plaintext results is full. If such a thing happens
   *  a thread that calls regularly popPlaintextResult must be used.
  **/	
  void extractReply(uint64_t maxFileBytesize);
			
  /** 
   *  Can be called on a separate thread
   *  Function to get a pointer to a char* result element
   *  Returns false when the queue is over (true otherwise) and waits when its empty
  **/
  bool popPlaintextResult(char** result);

  /**
   *  Get plaintext reply size
  **/ 
  uint64_t getPlaintextReplyBytesize();
  
  /** 
   *  Getter for nbPlaintextReplies, the number of plaintext replies that will be generated
   *  in the extraction processs
  **/
  uint64_t getnbPlaintextReplies(uint64_t maxFileBytesize);
  
private:
	shared_queue<char*> clearChunks;
	uint64_t nbPlaintextReplies;
};

#endif


#ifndef LOGER_HPP
#define LOGER_HPP

#include <filesystem>

class loger_t {
  private:
    std::filesystem::path path_loger_file = "";

  public:
  	/**
  	 * 
  	 */
  	loger_t(std::filesystem::path path_loger_file_): path_loger_file(path_loger_file);
    
    /**
  	 * 
  	 */
    int run(
        int argc,
        char* argv[]
    );

  	/**
  	 * 
  	 */
  	~loger_t();
};

#endif // LOGER_HPP
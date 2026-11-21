#include "browser_store.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace glassglide {

namespace {

std::string failure(const char* operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

} // namespace

BrowserStore::~BrowserStore()
{
    if (directory_ >= 0)
        close(directory_);
}

bool BrowserStore::open(const std::string& directory, std::string& error)
{
    if (directory_ >= 0) {
        error = "Archivio gia aperto";
        return false;
    }
    if (mkdir(directory.c_str(), 0700) < 0 && errno != EEXIST) {
        error = failure("Impossibile creare la cartella dati");
        return false;
    }
    int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        error = failure("Impossibile aprire la cartella dati");
        return false;
    }
    struct stat status;
    if (fstat(fd, &status) < 0) {
        error = failure("Impossibile verificare la cartella dati");
        close(fd);
        return false;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid() || (status.st_mode & 0077)) {
        error = "Proprieta o permessi della cartella dati non sicuri";
        close(fd);
        return false;
    }
    directory_ = fd;
    error.clear();
    return true;
}

BrowserStore::LoadResult BrowserStore::load(std::string& data, std::string& error) const
{
    if (directory_ < 0) {
        error = "Archivio non disponibile";
        return LoadResult::Error;
    }
    int fd = openat(directory_, "state", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            error.clear();
            return LoadResult::Missing;
        }
        error = failure("Impossibile leggere i dati");
        return LoadResult::Error;
    }
    struct stat status;
    if (fstat(fd, &status) < 0) {
        error = failure("Impossibile verificare i dati");
        close(fd);
        return LoadResult::Error;
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid()
        || (status.st_mode & 0077) || status.st_size < 0 || status.st_size > 8192) {
        error = "File dati non valido o non sicuro";
        close(fd);
        return LoadResult::Error;
    }
    std::string contents;
    char buffer[1024];
    for (;;) {
        ssize_t size = read(fd, buffer, sizeof(buffer));
        if (size < 0 && errno == EINTR)
            continue;
        if (size < 0) {
            error = failure("Errore di lettura dei dati");
            close(fd);
            return LoadResult::Error;
        }
        if (!size)
            break;
        contents.append(buffer, static_cast<std::size_t>(size));
        if (contents.size() > 8192) {
            error = "File dati troppo grande";
            close(fd);
            return LoadResult::Error;
        }
    }
    if (close(fd) < 0) {
        error = failure("Errore di chiusura dei dati");
        return LoadResult::Error;
    }
    data = std::move(contents);
    error.clear();
    return LoadResult::Loaded;
}

bool BrowserStore::save(const std::string& data, std::string& error)
{
    if (directory_ < 0 || data.size() > 8192) {
        error = "Archivio non disponibile o dati troppo grandi";
        return false;
    }
    std::string temporary = ".state-" + std::to_string(getpid()) + "-" + std::to_string(++counter_);
    int fd = openat(directory_, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = failure("Impossibile creare i dati temporanei");
        return false;
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
        ssize_t written = write(fd, data.data() + offset, data.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            if (!written)
                errno = EIO;
            error = failure("Errore di scrittura dei dati");
            close(fd);
            unlinkat(directory_, temporary.c_str(), 0);
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (fsync(fd) < 0) {
        error = failure("Impossibile sincronizzare i dati");
        close(fd);
        unlinkat(directory_, temporary.c_str(), 0);
        return false;
    }
    if (close(fd) < 0) {
        error = failure("Errore di chiusura dei dati");
        unlinkat(directory_, temporary.c_str(), 0);
        return false;
    }
    if (renameat(directory_, temporary.c_str(), directory_, "state") < 0) {
        error = failure("Impossibile aggiornare i dati");
        unlinkat(directory_, temporary.c_str(), 0);
        return false;
    }
    if (fsync(directory_) < 0) {
        error = failure("Impossibile sincronizzare la cartella dati");
        return false;
    }
    error.clear();
    return true;
}

} // namespace glassglide

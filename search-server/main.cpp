#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double EPSILON = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    Document() = default;

    Document(int id, double relevance, int rating)
        : id(id)
        , relevance(relevance)
        , rating(rating) {
    }

    int id = 0;
    double relevance = 0;
    int rating = 0;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    inline static constexpr int INVALID_DOCUMENT_ID = -1;

    SearchServer() = default;
    explicit SearchServer(const string& stop_words_text) {
        if (!IsValidWord(stop_words_text)) {
            throw invalid_argument("Invalid stop words");
        }
        for (const string& word : SplitIntoWords(stop_words_text)) {
            stop_words_.insert(word);
        }
    }
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& container) {
        for (const auto& word : container) {
            if (!IsValidWord(word)) {
                throw invalid_argument("Invalid stop words");
            }
            stop_words_.insert(word);
        }
    }

    void SetStopWords(const string& text) {
        if (!IsValidWord(text)) {
            throw invalid_argument("Invalid stop words");
        }
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status,
                     const vector<int>& ratings) {
        if (document_id < 0 || documents_.count(document_id) || !IsValidWord(document)) {
            throw invalid_argument("Invalid document");
        }
        
        index_id_[GetDocumentCount()] = document_id;
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status});
    }

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(const string& raw_query, DocumentPredicate document_predicate) const {
        
        const auto query = ParseQuery(raw_query);                
        if (!IsValidWord(raw_query) || !query.has_value()){
            throw invalid_argument("Invalid query");
        }
        
        auto matched_documents = FindAllDocuments(*query);

        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                 if (abs(lhs.relevance - rhs.relevance) < EPSILON) {
                     return lhs.rating > rhs.rating;
                 } else {
                     return lhs.relevance > rhs.relevance;
                 }
             });

        vector<Document> temp;
        for (const auto& document : matched_documents) {
            DocumentStatus status = documents_.at(document.id).status;
            if (document_predicate(document.id, status, document.rating)) {
                temp.push_back(document);
            }
        }

        matched_documents = temp;

        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status_doc) const {
        return FindTopDocuments(raw_query, [&status_doc](int document_id, DocumentStatus status, int rating) {return status_doc == status;});
    }

    
    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }
    

    int GetDocumentCount() const {
        return documents_.size();
    }

    int GetDocumentId(int index) const {
        if (index < 0 || index >= GetDocumentCount()) {
            throw out_of_range("Invalid index");
        }
        return index_id_.at(index);
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        auto query = ParseQuery(raw_query);                          
        if (!IsValidWord(raw_query) || !query.has_value()){
            throw invalid_argument("Invalid query");
        }
        
        vector<string> matched_words;
        for (const string& word : query->plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query->minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return tuple{matched_words, documents_.at(document_id).status};
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    map<int, int> index_id_;

    static bool IsValidWord(const string& word) {
        // A valid word must not contain special characters
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
        });
    }

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = accumulate(ratings.begin(), ratings.end(), 0);
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    optional<QueryWord> ParseQueryWord(string text) const {
        bool is_minus = false;
        int count_minus = 0;
        while (text.size() > 1 && text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
            ++count_minus;
            if (count_minus > 1){
                return nullopt;
            }
        }
        if (text[0] == '-' && text.size() == 1){
            return nullopt;
        }
        return QueryWord{text, is_minus, IsStopWord(text)};
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    optional<Query> ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            if (const auto query_word = ParseQueryWord(word)) {
                if (!query_word->is_stop) {
                    if (query_word->is_minus) {
                        query.minus_words.insert(query_word->data);
                    } else {
                        query.plus_words.insert(query_word->data);
                    }
                }
            } else {
                return nullopt;
            }   

        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    vector<Document> FindAllDocuments(const Query& query) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto &[document_id, term_freq] : word_to_document_freqs_.at(word)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto &[document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto &[document_id, relevance] : document_to_relevance) {
            matched_documents.push_back(
                {document_id, relevance, documents_.at(document_id).rating});
        }
        return matched_documents;
    }
};

int main(int argc, char const *argv[])
{
    /* code */
    return 0;
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <filesystem>

using namespace std;

// helprs
static inline string trim(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// CSV splitter që respekton thonjëzat "..."
static vector<string> splitCSV(const string& line) {
    vector<string> out;
    string cur;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
}

static bool isValidDateYYYYMMDD(const string& s) {
    if (s.size() != 10) return false;
    if (s[4] != '-' || s[7] != '-') return false;
    for (int i : {0,1,2,3,5,6,8,9}) if (!isdigit((unsigned char)s[i])) return false;

    int y = stoi(s.substr(0,4));
    int m = stoi(s.substr(5,2));
    int d = stoi(s.substr(8,2));
    if (y < 1900 || m < 1 || m > 12 || d < 1) return false;

    auto isLeap = [&](int yy) {
        return (yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0);
    };

    int mdays = 31;
    switch (m) {
        case 2: mdays = isLeap(y) ? 29 : 28; break;
        case 4: case 6: case 9: case 11: mdays = 30; break;
        default: mdays = 31; break;
    }
    return d <= mdays;
}

// Kthen ditët që nga 1970-01-01 (epoch). C++17 safe.
static int daysSinceEpoch19700101(const string& s) {
    int y = stoi(s.substr(0, 4));
    unsigned m = (unsigned)stoi(s.substr(5, 2));
    unsigned d = (unsigned)stoi(s.substr(8, 2));

    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int days = era * 146097 + (int)doe - 719468; // 1970-01-01 offset
    return days;
}

// 0=Mon ... 6=Sun
static int weekdayMon0_fromDaysSinceEpoch(int daysSinceEpoch) {
    // 1970-01-01 ishte E ENJTE (Thu). Në mon0: Thu=3
    int mon0 = (daysSinceEpoch + 3) % 7;
    if (mon0 < 0) mon0 += 7;
    return mon0;
}

static string weekdayNameAL(int mon0) {
    static vector<string> names = {
        "E HENE", "E MARTE", "E MERKURE", "E ENJTE", "E PREMTE", "E SHTUNE", "E DIELE"
    };
    if (mon0 < 0 || mon0 > 6) return "E PANJOHUR";
    return names[mon0];
}

// ===================== Data Models =====================
struct Topic {
    int nr = 0;
    string tema;
    string detyraShtepie;
    string fqDitari;
    string situata;
    string metodologjia;
    string vleresimi;
    string burimet;
    string rezultatet;
    string fjaletKyce;
    string lidhja;
    string organizimi;
    string detyreShtepieField;
    string kompetencat;
    int java = 0; 
};

struct ScheduleCell {
    bool empty = true;
    string lenda;   
    string klasa;   
};

struct LessonPlan {
    string date;
    string dita;
    int ora = 0;
    string klasa;
    string lenda;
    Topic topic;
};

static void mustOpenOrExit(ifstream& in, const string& path) {
    in.open(path);
    if (!in.is_open()) {
        cerr << "Nuk u hap file: " << path << "\n";
        cerr << "Folderi aktual (ku programi po kerkon file): "
             << filesystem::current_path().string() << "\n";
        cerr << "Sigurohu qe " << path << " eshte ne te njejtin folder me ditari.exe (ose perdor path absolut).\n";
        exit(1);
    }
}

static unordered_set<string> readHolidays(const string& path) {
    unordered_set<string> holidays;

    ifstream in;
    mustOpenOrExit(in, path);

    string header;
    getline(in, header); // date,reason...
    string line;
    while (getline(in, line)) {
        if (trim(line).empty()) continue;
        auto cols = splitCSV(line);
        if (!cols.empty()) {
            string d = trim(cols[0]);
            if (isValidDateYYYYMMDD(d)) holidays.insert(d);
        }
    }
    return holidays;
}

static unordered_map<int, vector<Topic>> readTopicsByWeek(const string& path) {
    unordered_map<int, vector<Topic>> byWeek;

    ifstream in;
    mustOpenOrExit(in, path);

    string headerLine;
    if (!getline(in, headerLine)) return byWeek;
    auto headers = splitCSV(headerLine);

    auto idxOf = [&](const string& name) -> int {
        for (int i = 0; i < (int)headers.size(); i++) {
            if (trim(headers[i]) == name) return i;
        }
        return -1;
    };

    int iNr = idxOf("Nr");
    int iTema = idxOf("Tema");
    int iDetyra = idxOf("Detyra Shtepie");
    int iFq = idxOf("Fq Ditari");
    int iSituata = idxOf("Situata e të nxënit:");
    int iMetod = idxOf("Metodologjia dhe veprimtaritë e nxënësve:");
    int iVler = idxOf("Vlerësimi:");
    int iBur = idxOf("Burimet:");
    int iRez = idxOf("Rezultatet e të nxënit sipas kompetencave kyç:");
    int iFjale = idxOf("Fjalët kyç:");
    int iLidh = idxOf("Lidhja me fusha të tjera ose me temat ndërkurrikulare:");
    int iOrg = idxOf("Organizimi i orës së mësimit:");
    int iDetField = idxOf("Detyre shtepie:");
    int iKomp = idxOf("Kompetencat që përfitojnë:");
    int iJava = idxOf("Java");

    string line;
    while (getline(in, line)) {
        if (trim(line).empty()) continue;
        auto c = splitCSV(line);

        auto getS = [&](int idx)->string {
            if (idx < 0 || idx >= (int)c.size()) return "";
            return trim(c[idx]);
        };
        auto getI = [&](int idx)->int {
            string s = getS(idx);
            if (s.empty()) return 0;
            try { return stoi(s); } catch (...) { return 0; }
        };

        Topic t;
        t.nr = getI(iNr);
        t.tema = getS(iTema);
        t.detyraShtepie = getS(iDetyra);
        t.fqDitari = getS(iFq);
        t.situata = getS(iSituata);
        t.metodologjia = getS(iMetod);
        t.vleresimi = getS(iVler);
        t.burimet = getS(iBur);
        t.rezultatet = getS(iRez);
        t.fjaletKyce = getS(iFjale);
        t.lidhja = getS(iLidh);
        t.organizimi = getS(iOrg);
        t.detyreShtepieField = getS(iDetField);
        t.kompetencat = getS(iKomp);
        t.java = getI(iJava);

        if (t.nr > 0 && t.java > 0) byWeek[t.java].push_back(t);
    }

    for (auto& kv : byWeek) {
        auto& vec = kv.second;
        sort(vec.begin(), vec.end(), [](const Topic& a, const Topic& b){ return a.nr < b.nr; });
    }

    return byWeek;
}

static vector<vector<ScheduleCell>> readWeeklyScheduleGrid(const string& path) {
    vector<vector<ScheduleCell>> grid(5, vector<ScheduleCell>(8)); // weekday 0..4, hour 1..7

    ifstream in;
    mustOpenOrExit(in, path);

    string headerLine;
    if (!getline(in, headerLine)) return grid;

    string line;
    while (getline(in, line)) {
        if (trim(line).empty()) continue;
        auto cols = splitCSV(line);
        if (cols.size() < 2) continue;

        int hour = 0;
        try { hour = stoi(trim(cols[0])); } catch (...) { continue; }
        if (hour < 1 || hour > 7) continue;

        for (int wd = 0; wd < 5; wd++) {
            int colIndex = 1 + wd;
            if (colIndex >= (int)cols.size()) continue;

            string cell = trim(cols[colIndex]);
            if (cell.empty()) {
                grid[wd][hour].empty = true;
                continue;
            }

            ScheduleCell sc;
            sc.empty = false;

            size_t sp = cell.find(' ');
            if (sp == string::npos) {
                sc.lenda = cell;
                sc.klasa = "";
            } else {
                sc.lenda = trim(cell.substr(0, sp));
                string rest = trim(cell.substr(sp + 1)); // p.sh 3-6

                size_t dash = rest.find('-');
                if (dash != string::npos) {
                    string vit = trim(rest.substr(0, dash));
                    string nr = trim(rest.substr(dash + 1));
                    string roman = (vit == "3") ? "III" : (vit == "4" ? "IV" : vit);
                    sc.klasa = roman + nr;
                } else {
                    sc.klasa = rest;
                }
            }

            grid[wd][hour] = sc;
        }
    }

    return grid;
}

static int teachingWeekIndex(const string& dateStr, const string& termStart = "2025-09-08") {
    int d = daysSinceEpoch19700101(dateStr);
    int start = daysSinceEpoch19700101(termStart);
    int diff = d - start;
    if (diff < 0) return 0;
    return diff / 7 + 1;
}

static int nthLessonOfSubjectInWeek(
    const vector<vector<ScheduleCell>>& grid,
    int weekdayMon0_0to4,
    int hour,
    const string& subject
) {
    int count = 0;
    for (int wd = 0; wd < 5; wd++) {
        for (int h = 1; h <= 7; h++) {
            const auto& c = grid[wd][h];
            if (!c.empty && c.lenda == subject) {
                if (wd < weekdayMon0_0to4 || (wd == weekdayMon0_0to4 && h <= hour)) {
                    count++;
                }
            }
        }
    }
    return count;
}

static bool getTopicForLesson(
    const unordered_map<int, vector<Topic>>& topicsByWeek,
    int weekIndex,
    int lessonIndexWithinWeek,
    Topic& outTopic
) {
    auto it = topicsByWeek.find(weekIndex);
    if (it == topicsByWeek.end()) return false;
    const auto& vec = it->second;
    int idx = lessonIndexWithinWeek - 1;
    if (idx < 0 || idx >= (int)vec.size()) return false;
    outTopic = vec[idx];
    return true;
}
static void printLesson(ostream& os, const LessonPlan& lp) {
    os << "Fusha:\n";
    os << "Lënda:\t" << lp.lenda << "\n";
    os << "Tema mësimore:\t" << lp.topic.tema << "\n";
    os << "Klasa:\t" << lp.klasa << " ora " << lp.ora << "\n";
    os << "Situata e të nxënit:\t" << lp.topic.situata << "\n";
    os << "Rezultatet e të nxënit:\t" << lp.topic.rezultatet << "\n";
    os << "Fjalët kyç:\t" << lp.topic.fjaletKyce << "\n";
    os << "Lidhja me fusha të tjera:\t" << lp.topic.lidhja << "\n";
    os << "Burimet:\t" << lp.topic.burimet << "\n";
    os << "Metodologjia:\t" << lp.topic.metodologjia << "\n";
    os << "Organizimi i orës:\t" << lp.topic.organizimi << "\n";
    os << "Kompetencat:\t" << lp.topic.kompetencat << "\n";
    os << "Vlerësimi:\t" << lp.topic.vleresimi << "\n";
    os << "Detyrat dhe puna e pavarur:\t" << lp.topic.detyraShtepie << "\n";
    os << "Detyre shtepie:\t" << lp.topic.detyreShtepieField << "\n";
    os << "--------------------------------------------\n\n";
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    const string STUDENT_NAME = "EMRI MBIEMRI"; 
    const string TERM_START = "2025-09-08";
    const string FILE_ORARI = "Orari.csv";
    const string FILE_CPP_TOPICS = "Plani_Lenda1.csv";
    const string FILE_JAVA_TOPICS = "Plani_Lenda2.csv";
    const string FILE_PUSHIME = "Pushime.csv";
    auto scheduleGrid = readWeeklyScheduleGrid(FILE_ORARI);
    auto cppTopics = readTopicsByWeek(FILE_CPP_TOPICS);
    auto javaTopics = readTopicsByWeek(FILE_JAVA_TOPICS);
    auto holidays = readHolidays(FILE_PUSHIME);
    string dateStr;
    while (true) {
        cout << "Shkruaj daten (YYYY-MM-DD): " << flush;
        if (!(cin >> dateStr)) return 0;
        if (isValidDateYYYYMMDD(dateStr)) break;
        cout << "Format gabim. Provo perseri.\n";
    }

    cout << "\n================ DITARI DITOR ================\n";
    cout << "Nxenesi: " << STUDENT_NAME << "\n";
    cout << "Data: " << dateStr << "\n";

    if (holidays.find(dateStr) != holidays.end()) {
        cout << "STATUS: Dite pushimi. Nuk ka mesim.\n";
        ofstream out("Ditari_Output.txt");
        out << "Nxenesi: " << STUDENT_NAME << "\n";
        out << "Data: " << dateStr << "\n";
        out << "STATUS: Dite pushimi. Nuk ka mesim.\n";
        return 0;
    }

    int d = daysSinceEpoch19700101(dateStr);
    int wd = weekdayMon0_fromDaysSinceEpoch(d); // 0..6
    cout << "Dita: " << weekdayNameAL(wd) << "\n";

    if (wd > 4) {
        cout << "STATUS: Fundjave. Nuk ka mesim.\n";
        ofstream out("Ditari_Output.txt");
        out << "Nxenesi: " << STUDENT_NAME << "\n";
        out << "Data: " << dateStr << "\n";
        out << "Dita: " << weekdayNameAL(wd) << "\n";
        out << "STATUS: Fundjave. Nuk ka mesim.\n";
        return 0;
    }

    int weekIdx = teachingWeekIndex(dateStr, TERM_START);
    cout << "Java mesimore: " << weekIdx << "\n";
    cout << "=============================================\n\n";

    ofstream out("Ditari_Output.txt");
    out << "Nxenesi: " << STUDENT_NAME << "\n";
    out << "Data: " << dateStr << "\n";
    out << "Dita: " << weekdayNameAL(wd) << "\n";
    out << "Java mesimore: " << weekIdx << "\n";
    out << "=============================================\n\n";

    bool foundAny = false;

    for (int hour = 1; hour <= 7; hour++) {
        const auto& cell = scheduleGrid[wd][hour];
        if (cell.empty) continue;

        foundAny = true;

        LessonPlan lp;
        lp.date = dateStr;
        lp.dita = weekdayNameAL(wd);
        lp.ora = hour;
        lp.klasa = cell.klasa;
        lp.lenda = cell.lenda;

        int lessonIndex = nthLessonOfSubjectInWeek(scheduleGrid, wd, hour, cell.lenda);

        Topic t;
        bool ok = false;

        if (cell.lenda == "C++" || cell.lenda == "C") {
            ok = getTopicForLesson(cppTopics, weekIdx, lessonIndex, t);
        } else if (cell.lenda == "JAVA") {
            ok = getTopicForLesson(javaTopics, weekIdx, lessonIndex, t);
        }

        if (!ok) {
            t.tema = "(Nuk u gjet tema per kete ore)";
            t.situata = "-";
            t.metodologjia = "-";
            t.vleresimi = "-";
            t.detyraShtepie = "-";
            t.detyreShtepieField = "-";
            t.burimet = "-";
            t.rezultatet = "-";
            t.fjaletKyce = "-";
            t.lidhja = "-";
            t.organizimi = "-";
            t.kompetencat = "-";
        }

        lp.topic = t;

        printLesson(cout, lp);
        printLesson(out, lp);
    }

    if (!foundAny) {
        cout << "STATUS: Nuk ka ore te planifikuara per kete dite.\n";
        out << "STATUS: Nuk ka ore te planifikuara per kete dite.\n";
    }

    return 0;
}

import XCTest
import SwiftTreeSitter
import TreeSitterCabalProject

final class TreeSitterCabalProjectTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_cabal_project())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading CabalProject grammar")
    }
}
